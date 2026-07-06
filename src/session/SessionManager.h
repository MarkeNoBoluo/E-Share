#pragma once

#include "control/DeviceInfo.h"
#include "control/DeviceDiscovery.h"
#include "config/ConfigLoader.h"
#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <memory>

// Forward declarations
class RtspServerManager;
class PushService;
class PullService;

enum class SessionState {
    Idle,
    Preparing,
    Pushing,
    Receiving,
    Stopping,
    Error
};

inline const char* sessionStateName(SessionState s) {
    switch (s) {
        case SessionState::Idle:      return "Idle";
        case SessionState::Preparing: return "Preparing";
        case SessionState::Pushing:   return "Pushing";
        case SessionState::Receiving: return "Receiving";
        case SessionState::Stopping:  return "Stopping";
        case SessionState::Error:     return "Error";
    }
    return "Unknown";
}

class SessionManager {
public:
    SessionManager(const EShareConfig& config, const std::string& localIP);
    ~SessionManager();

    // ── Sender side ──────────────────────────────────────────────────
    bool startPush(const OnlineDevice& target);
    void stopPush();

    // ── Receiver side (called from ControlServer callbacks) ──────────
    bool handlePlayRequest(const PlayRequest& req);
    void handleStopRequest();

    // ── Status ───────────────────────────────────────────────────────
    SessionState state() const { return m_state; }
    std::string activeSessionId() const;
    std::string targetDeviceId() const;
    std::string streamUrl() const;

    // ── Health check (call periodically from main loop) ──────────────
    // Returns false if a child process died unexpectedly
    bool checkHealth();

    // Callback for state change notifications
    using StateCallback = std::function<void(SessionState, const std::string& detail)>;
    void setStateCallback(StateCallback cb);

private:
    std::string generateSessionId() const;

    const EShareConfig& m_config;
    std::string m_localIP;

    std::unique_ptr<RtspServerManager> m_rtspServer;
    std::unique_ptr<PushService> m_pushService;
    std::unique_ptr<PullService> m_pullService;

    std::atomic<SessionState> m_state{SessionState::Idle};
    mutable std::mutex m_mutex;

    std::string m_sessionId;
    std::string m_targetDeviceId;
    std::string m_targetIP;
    uint16_t    m_targetPort = 0;
    std::string m_streamUrl;

    StateCallback m_stateCallback;

    void setState(SessionState s, const std::string& detail = "");
};
