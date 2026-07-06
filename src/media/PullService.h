#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <windows.h>

class PullService {
public:
    PullService();
    ~PullService();

    bool start(const std::string& url, const std::string& transport = "tcp",
        const std::string& binPath = "bin/RTSP-Player.exe");
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
