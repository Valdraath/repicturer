// child process with captured stdout / stderr lines (utf-8), killable

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

class Subprocess
{
public:
    // stream: 1 = stdout, 2 = stderr; called from reader threads
    typedef std::function<void(int stream, const std::string& line)> LineCallback;

    Subprocess();
    ~Subprocess();

    bool Start(const std::string& exe, const std::vector<std::string>& args, LineCallback cb, std::string& err);
    int Wait();          // blocks until exit, returns exit code (-1000 when killed / failed)
    void Kill();
    bool Running() const { return running.load(); }

    static std::string QuoteForDisplay(const std::string& exe, const std::vector<std::string>& args);

private:
    void ReaderLoop(int stream);

    LineCallback callback;
    std::thread readers[2];
    std::atomic<bool> running;
    std::atomic<bool> killed;
#ifdef _WIN32
    void* hProcess = nullptr;
    void* hJob = nullptr;
    void* hRead[2] = {nullptr, nullptr};
#else
    int pid = -1;
    int fdRead[2] = {-1, -1};
#endif
};
