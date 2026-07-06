#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <windows.h>

struct PushConfig {
    std::string url;
    int         screenIndex   = 0;
    std::string outputSize    = "1920x1080";
    int         fps           = 30;
    int         bitrateKbps   = 8000;
    std::string transport     = "tcp";
};

class PushService {
public:
    PushService();
    ~PushService();

    bool start(const PushConfig& config, const std::string& binPath = "bin/RTSP-Pusher.exe");
    void stop();

    bool isRunning() const { return m_running; }
    bool checkAlive() const;
    DWORD exitCode() const;

private:
    PROCESS_INFORMATION m_pi{};
    HANDLE m_stdinWrite = nullptr;
    HANDLE m_stdoutRead = nullptr;
    HANDLE m_stderrRead = nullptr;
    std::atomic<bool> m_running{false};
    std::thread m_stdoutThread;
    std::thread m_stderrThread;
    DWORD m_exitCode = 0;
};
