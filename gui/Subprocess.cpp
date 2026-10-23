#include "Subprocess.h"
#include "Util.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#endif

Subprocess::Subprocess() : running(false), killed(false) {}

Subprocess::~Subprocess()
{
    if (running) Kill();
    Wait();
}

#ifdef _WIN32

static std::wstring Widen(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

// quoting rules of CommandLineToArgvW / the msvcrt parser
static std::wstring QuoteArg(const std::wstring& a)
{
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring r = L"\"";
    for (size_t i = 0;; i++)
    {
        size_t bs = 0;
        while (i < a.size() && a[i] == L'\\') { bs++; i++; }
        if (i == a.size()) { r.append(bs * 2, L'\\'); break; }
        if (a[i] == L'"') { r.append(bs * 2 + 1, L'\\'); r.push_back(L'"'); }
        else { r.append(bs, L'\\'); r.push_back(a[i]); }
    }
    r.push_back(L'"');
    return r;
}

bool Subprocess::Start(const std::string& exe, const std::vector<std::string>& args, LineCallback cb, std::string& err)
{
    callback = cb;
    killed = false;

    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE w[2] = {NULL, NULL};
    for (int i = 0; i < 2; i++)
    {
        HANDLE r;
        if (!CreatePipe(&r, &w[i], &sa, 0)) { err = "CreatePipe failed"; return false; }
        SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);
        hRead[i] = r;
    }

    std::wstring cmd = QuoteArg(Widen(exe));
    for (const auto& a : args) cmd += L" " + QuoteArg(Widen(a));

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = w[0];
    si.hStdError = w[1];
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    std::wstring wexe = Widen(exe);
    std::wstring cwd = Widen(ParentPath(exe));
    BOOL ok = CreateProcessW(wexe.c_str(), &cmd[0], NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
        NULL, cwd.empty() ? NULL : cwd.c_str(), &si, &pi);
    CloseHandle(w[0]);
    CloseHandle(w[1]);
    if (!ok)
    {
        err = Format("CreateProcess failed (error %lu)", (unsigned long)GetLastError());
        CloseHandle(hRead[0]);
        CloseHandle(hRead[1]);
        hRead[0] = hRead[1] = NULL;
        return false;
    }

    // kill the converter together with the gui
    hJob = CreateJobObjectW(NULL, NULL);
    if (hJob)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
        memset(&li, 0, sizeof(li));
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &li, sizeof(li));
        AssignProcessToJobObject(hJob, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    hProcess = pi.hProcess;
    running = true;

    for (int i = 0; i < 2; i++)
        readers[i] = std::thread(&Subprocess::ReaderLoop, this, i + 1);
    return true;
}

void Subprocess::ReaderLoop(int stream)
{
    HANDLE h = hRead[stream - 1];
    std::string buf;
    char tmp[4096];
    DWORD n;
    while (ReadFile(h, tmp, sizeof(tmp), &n, NULL) && n > 0)
    {
        buf.append(tmp, n);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos)
        {
            std::string line = buf.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            buf.erase(0, pos + 1);
            if (callback) callback(stream, line);
        }
    }
    if (!buf.empty() && callback) callback(stream, buf);
}

int Subprocess::Wait()
{
    if (!hProcess) return -1000;
    WaitForSingleObject((HANDLE)hProcess, INFINITE);
    for (int i = 0; i < 2; i++)
        if (readers[i].joinable()) readers[i].join();
    DWORD code = (DWORD)-1000;
    GetExitCodeProcess((HANDLE)hProcess, &code);
    CloseHandle((HANDLE)hProcess);
    hProcess = nullptr;
    for (int i = 0; i < 2; i++)
        if (hRead[i]) { CloseHandle((HANDLE)hRead[i]); hRead[i] = nullptr; }
    if (hJob) { CloseHandle((HANDLE)hJob); hJob = nullptr; }
    running = false;
    return killed ? -1000 : (int)code;
}

void Subprocess::Kill()
{
    killed = true;
    if (hProcess) TerminateProcess((HANDLE)hProcess, 1);
}

#else // posix

bool Subprocess::Start(const std::string& exe, const std::vector<std::string>& args, LineCallback cb, std::string& err)
{
    callback = cb;
    killed = false;

    int out[2], errp[2];
    if (pipe(out) != 0 || pipe(errp) != 0) { err = "pipe failed"; return false; }

    std::vector<std::string> argv_s;
    argv_s.push_back(exe);
    argv_s.insert(argv_s.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& s : argv_s) argv.push_back(&s[0]);
    argv.push_back(nullptr);
    const std::string cwd = ParentPath(exe);

    pid_t p = fork();
    if (p < 0)
    {
        err = "fork failed";
        close(out[0]); close(out[1]); close(errp[0]); close(errp[1]);
        return false;
    }
    if (p == 0)
    {
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        dup2(out[1], 1);
        dup2(errp[1], 2);
        close(out[0]); close(out[1]); close(errp[0]); close(errp[1]);
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) {}
        execv(exe.c_str(), argv.data());
        fprintf(stderr, "exec %s failed: %s\n", exe.c_str(), strerror(errno));
        _exit(127);
    }
    close(out[1]);
    close(errp[1]);
    fdRead[0] = out[0];
    fdRead[1] = errp[0];
    pid = p;
    running = true;
    for (int i = 0; i < 2; i++)
        readers[i] = std::thread(&Subprocess::ReaderLoop, this, i + 1);
    return true;
}

void Subprocess::ReaderLoop(int stream)
{
    int fd = fdRead[stream - 1];
    std::string buf;
    char tmp[4096];
    for (;;)
    {
        ssize_t n = read(fd, tmp, sizeof(tmp));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        buf.append(tmp, (size_t)n);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos)
        {
            std::string line = buf.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            buf.erase(0, pos + 1);
            if (callback) callback(stream, line);
        }
    }
    if (!buf.empty() && callback) callback(stream, buf);
}

int Subprocess::Wait()
{
    if (pid <= 0) return -1000;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    for (int i = 0; i < 2; i++)
        if (readers[i].joinable()) readers[i].join();
    for (int i = 0; i < 2; i++)
        if (fdRead[i] >= 0) { close(fdRead[i]); fdRead[i] = -1; }
    pid = -1;
    running = false;
    if (killed) return -1000;
    if (WIFEXITED(status)) return (int)(signed char)WEXITSTATUS(status);
    return -1001; // crashed (signal), not a user cancel
}

void Subprocess::Kill()
{
    killed = true;
    if (pid > 0) kill(pid, SIGKILL);
}

#endif

std::string Subprocess::QuoteForDisplay(const std::string& exe, const std::vector<std::string>& args)
{
    auto q = [](const std::string& s) {
        if (!s.empty() && s.find_first_of(" \t\"") == std::string::npos) return s;
        return "\"" + s + "\"";
    };
    std::string r = q(exe);
    for (const auto& a : args) r += " " + q(a);
    return r;
}
