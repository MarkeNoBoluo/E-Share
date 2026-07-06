#pragma once

#include "control/DeviceInfo.h"
#include <functional>
#include <string>
#include <thread>
#include <atomic>
#include <memory>

namespace httplib { class Server; }

// Callbacks invoked when the control server receives requests from peers
struct ControlCallbacks {
    // Return the current DeviceInfo status for /status queries
    std::function<DeviceInfo()> onStatus;

    // /session/play — return true to accept, false to reject
    std::function<bool(const PlayRequest&)> onPlay;

    // /session/stop — return true if handled
    std::function<bool()> onStop;

    // /session/ping — simple keepalive
    std::function<void()> onPing;
};

class ControlServer {
public:
    ControlServer();
    ~ControlServer();

    bool start(uint16_t port, ControlCallbacks callbacks);
    void stop();

    // Update the status returned by /status (for push-based updates)
    void setStatusCallback(std::function<DeviceInfo()> cb);

private:
    void serveLoop();

    uint16_t m_port = 0;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    ControlCallbacks m_callbacks;
    std::unique_ptr<httplib::Server> m_svr;
    int m_serverFd = -1;
};
