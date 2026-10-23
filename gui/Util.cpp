#include "Util.h"
#ifndef _WIN32
#include <pwd.h>
#include <unistd.h>
#endif
#include <stdlib.h>

#include <fstream>
#include <sstream>
#include <algorithm>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

bool ReadTextFileUtf8(const std::string& path, std::string& out)
{
    std::ifstream ifs(U8Path(path), std::ios::binary);
    if (!ifs) return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    out = ss.str();
    return true;
}

bool WriteTextFileUtf8(const std::string& path, const std::string& data)
{
    std::ofstream ofs(U8Path(path), std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    ofs.write(data.data(), (std::streamsize)data.size());
    return (bool)ofs;
}

std::string GetExeDir()
{
#ifdef _WIN32
    wchar_t buf[4096];
    DWORD n = GetModuleFileNameW(NULL, buf, 4096);
    return PathU8(fs::path(std::wstring(buf, n)).parent_path());
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    buf[n] = 0;
    return PathU8(fs::path(buf).parent_path());
#endif
}

std::string GetTempDir()
{
    std::error_code ec;
    fs::path p = fs::temp_directory_path(ec);
    if (ec) return GetExeDir();
    return PathU8(p);
}

void GetUserUILanguage(int& primary, int& sub)
{
#ifdef _WIN32
    LANGID id = GetUserDefaultUILanguage();
    primary = PRIMARYLANGID(id);
    sub = SUBLANGID(id);
#else
    // map the posix locale to the windows language ids used by LangList.txt
    const char* env = getenv("LC_ALL");
    if (!env || !*env) env = getenv("LC_MESSAGES");
    if (!env || !*env) env = getenv("LANG");
    std::string l = env ? env : "en_US";
    std::string lang = l.substr(0, 2);
    std::string region = l.size() >= 5 ? l.substr(3, 2) : "";
    primary = 0x09;
    sub = 0x01;
    struct M { const char* code; int pri; };
    static const M map[] = {
        {"en", 0x09}, {"de", 0x07}, {"es", 0x0A}, {"fr", 0x0C}, {"ru", 0x19}, {"tr", 0x1F}, {"ja", 0x11},
        {"ko", 0x12}, {"zh", 0x04}, {"pt", 0x16}, {"cs", 0x05}, {"it", 0x10},
    };
    for (const auto& m : map)
        if (lang == m.code) primary = m.pri;
    if (primary == 0x04)
        sub = (region == "CN" || region == "SG") ? 0x02 : 0x01;
#endif
}

std::string Format(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[4096];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return std::string();
    if (n < (int)sizeof(buf)) return std::string(buf, n);
    std::string big(n + 1, '\0');
    va_start(ap, fmt);
    vsnprintf(&big[0], big.size(), fmt, ap);
    va_end(ap);
    big.resize(n);
    return big;
}

std::string ToLowerAscii(std::string s)
{
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return s.substr(a, b - a);
}

bool ParseInt(const std::string& s, long& v)
{
    if (s.empty()) return false;
    char* end = nullptr;
    v = strtol(s.c_str(), &end, 10);
    return end && *end == '\0';
}

bool ParseDouble(const std::string& s, double& v)
{
    if (s.empty()) return false;
    char* end = nullptr;
    v = strtod(s.c_str(), &end);
    return end && *end == '\0';
}

std::vector<std::string> Split(const std::string& s, char sep, bool drop_empty)
{
    std::vector<std::string> v;
    size_t pos = 0;
    while (pos <= s.size())
    {
        size_t c = s.find(sep, pos);
        if (c == std::string::npos) c = s.size();
        std::string t = s.substr(pos, c - pos);
        if (!(drop_empty && t.empty())) v.push_back(t);
        pos = c + 1;
    }
    return v;
}

std::string DoubleToString(double v)
{
    return Format("%f", v);
}

// ---------------------------------------------------------------- ini

bool IniFile::Load(const std::string& path)
{
    entries.clear();
    std::string data;
    if (!ReadTextFileUtf8(path, data)) return false;
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
        data.erase(0, 3);
    std::istringstream is(data);
    std::string line, section;
    while (std::getline(is, line))
    {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[')
        {
            size_t e = line.find(']');
            section = line.substr(1, e == std::string::npos ? std::string::npos : e - 1);
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        Set(section, Trim(line.substr(0, eq)), Trim(line.substr(eq + 1)));
    }
    return true;
}

bool IniFile::Save(const std::string& path) const
{
    std::string out;
    std::vector<std::string> sections;
    for (const auto& e : entries)
        if (std::find(sections.begin(), sections.end(), e.section) == sections.end()) sections.push_back(e.section);
    for (const auto& s : sections)
    {
        out += "[" + s + "]\r\n";
        for (const auto& e : entries)
            if (e.section == s) out += e.key + "=" + e.value + "\r\n";
    }
    return WriteTextFileUtf8(path, out);
}

std::string IniFile::Get(const std::string& section, const std::string& key, const std::string& def) const
{
    for (const auto& e : entries)
        if (e.section == section && e.key == key) return e.value;
    return def;
}

int IniFile::GetInt(const std::string& section, const std::string& key, int def) const
{
    std::string v = Get(section, key, "");
    if (v.empty()) return def;
    // GetPrivateProfileInt semantics: leading integer part
    return (int)strtol(v.c_str(), nullptr, 10);
}

void IniFile::Set(const std::string& section, const std::string& key, const std::string& value)
{
    for (auto& e : entries)
        if (e.section == section && e.key == key) { e.value = value; return; }
    entries.push_back({section, key, value});
}

std::string HomeFolder()
{
#ifdef _WIN32
    const wchar_t* up = _wgetenv(L"USERPROFILE");
    if (up && *up) return PathU8(fs::path(up));
    const wchar_t* d = _wgetenv(L"HOMEDRIVE");
    const wchar_t* p = _wgetenv(L"HOMEPATH");
    if (d && p) return PathU8(fs::path(std::wstring(d) + p));
    return "C:\\";
#else
    const char* h = getenv("HOME");
    if (h && *h) return h;
    struct passwd* pw = getpwuid(getuid());
    return pw && pw->pw_dir ? pw->pw_dir : "/";
#endif
}

FILE* OpenFileU8(const std::string& path, const char* mode)
{
#ifdef _WIN32
    std::wstring m;
    for (const char* c = mode; *c; c++) m.push_back((wchar_t)*c);
    return _wfopen(U8Path(path).wstring().c_str(), m.c_str());
#else
    return fopen(path.c_str(), mode);
#endif
}

unsigned long GetCurrentProcessIdPortable()
{
#ifdef _WIN32
    return (unsigned long)GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}

void IniFile::Remove(const std::string& section, const std::string& key)
{
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.section == section && e.key == key; }), entries.end());
}
