#pragma once

#include <cstdint>
#include <string>
#include <atomic>

// winsock2 must come before windows.h
#include <winsock2.h>
#include <windows.h>

class RtspServerManager {
public:
    RtspServerManager();
    ~RtspServerManager();

    // Start MediaMTX on given port. Returns false if port is already in use or process fails.
    bool start(uint16_t port, const std::string& binPath = "bin/mediamtx.exe");

    // Stop the MediaMTX process.
    void stop();

    bool isRunning() const { return m_running; }

private:
    static bool waitForPort(uint16_t port, int timeoutMs);

    PROCESS_INFORMATION m_pi{};
    std::atomic<bool> m_running{false};
};
