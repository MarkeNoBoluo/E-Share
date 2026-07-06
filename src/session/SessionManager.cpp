#include "session/SessionManager.h"
#include "media/RtspServerManager.h"
#include "media/PushService.h"
#include "media/PullService.h"
#include "control/ControlClient.h"
#include "common/Logger.h"
#include <ctime>
#include <cstdio>

SessionManager::SessionManager(const EShareConfig& config, const std::string& localIP)
    : m_config(config), m_localIP(localIP) {
    m_rtspServer = std::make_unique<RtspServerManager>();
    m_pushService = std::make_unique<PushService>();
    m_pullService = std::make_unique<PullService>();
}

SessionManager::~SessionManager() {
    // Ensure everything is stopped
    if (m_pullService && m_pullService->isRunning()) m_pullService->stop();
    if (m_pushService && m_pushService->isRunning()) m_pushService->stop();
    if (m_rtspServer && m_rtspServer->isRunning()) m_rtspServer->stop();
}

void SessionManager::setStateCallback(StateCallback cb) {
    m_stateCallback = std::move(cb);
}

void SessionManager::setState(SessionState s, const std::string& detail) {
    auto old = m_state.exchange(s);
    if (old != s) {
        LOG_INFO("Session state: %s -> %s%s",
            sessionStateName(old), sessionStateName(s),
            detail.empty() ? "" : (std::string(" (") + detail + ")").c_str());
        if (m_stateCallback) m_stateCallback(s, detail);
    }
}

std::string SessionManager::generateSessionId() const {
    auto now = std::time(nullptr);
    std::tm tm;
    localtime_s(&tm, &now);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
    return std::string(buf);
}

std::string SessionManager::activeSessionId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sessionId;
}

std::string SessionManager::targetDeviceId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_targetDeviceId;
}

std::string SessionManager::streamUrl() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_streamUrl;
}

// ── Health check ─────────────────────────────────────────────────────────

bool SessionManager::checkHealth() {
    SessionState s = m_state;

    if (s == SessionState::Pushing) {
        if (m_pushService && m_pushService->isRunning() && !m_pushService->checkAlive()) {
            DWORD ec = m_pushService->exitCode();
            LOG_ERROR("PushService exited unexpectedly (code %lu)", ec);
            setState(SessionState::Error,
                std::string("PushService crashed (code ") + std::to_string(ec) + ")");
            return false;
        }
    }

    if (s == SessionState::Receiving) {
        if (m_pullService && m_pullService->isRunning() && !m_pullService->checkAlive()) {
            DWORD ec = m_pullService->exitCode();
            LOG_ERROR("PullService exited unexpectedly (code %lu)", ec);
            setState(SessionState::Error,
                std::string("PullService crashed (code ") + std::to_string(ec) + ")");
            return false;
        }
    }

    return true;
}

// ── Sender side ──────────────────────────────────────────────────────────

bool SessionManager::startPush(const OnlineDevice& target) {
    SessionState s = m_state;
    if (s != SessionState::Idle) {
        LOG_ERROR("Cannot start push: session in state %s", sessionStateName(s));
        return false;
    }

    setState(SessionState::Preparing);

    std::string sessionId = generateSessionId();
    uint16_t rtspPort = m_config.rtspPort;

    // Record target info
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sessionId = sessionId;
        m_targetDeviceId = target.info.deviceId;
        m_targetIP = target.info.ip;
        m_targetPort = target.info.controlPort;
        m_streamUrl = "rtsp://" + m_localIP + ":" + std::to_string(rtspPort)
            + "/live/" + sessionId;
    }

    // Step 1: Start RTSP Server
    LOG_INFO("Starting RTSP server on port %u...", rtspPort);
    if (!m_rtspServer->start(rtspPort)) {
        setState(SessionState::Error, "RTSP server start failed");
        return false;
    }

    // Step 2: Start PushService
    PushConfig pushCfg;
    pushCfg.url = m_streamUrl;
    pushCfg.outputSize = m_config.defaultOutputSize;
    pushCfg.fps = m_config.defaultFps;
    pushCfg.bitrateKbps = m_config.defaultBitrateKbps;
    pushCfg.transport = m_config.rtspTransport;

    LOG_INFO("Starting push to %s...", pushCfg.url.c_str());
    if (!m_pushService->start(pushCfg)) {
        setState(SessionState::Error, "PushService start failed");
        m_rtspServer->stop();
        return false;
    }

    // Step 3: Notify target device
    LOG_INFO("Notifying target %s:%u to play %s...",
        m_targetIP.c_str(), m_targetPort, m_streamUrl.c_str());

    auto result = ControlClient::sendPlay(m_targetIP, m_targetPort,
        sessionId, m_config.deviceName, m_streamUrl, false, true);

    if (!result.ok) {
        LOG_ERROR("Target device rejected or unreachable: HTTP %d, body: %s",
            result.httpStatus, result.body.c_str());
        m_pushService->stop();
        m_rtspServer->stop();
        setState(SessionState::Error, std::string("Target device error: ") + result.body);
        return false;
    }

    setState(SessionState::Pushing, "pushing to " + m_targetDeviceId);
    LOG_INFO("Session %s: pushing to %s", sessionId.c_str(), m_targetDeviceId.c_str());
    return true;
}

void SessionManager::stopPush() {
    SessionState s = m_state;
    if (s != SessionState::Pushing && s != SessionState::Error) {
        LOG_WARN("Cannot stop push: no active push session");
        return;
    }

    setState(SessionState::Stopping);

    // Notify target
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_targetIP.empty()) {
            ControlClient::sendStop(m_targetIP, m_targetPort);
        }
    }

    // Stop push first
    m_pushService->stop();

    // Stop RTSP server
    m_rtspServer->stop();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sessionId.clear();
        m_targetDeviceId.clear();
        m_targetIP.clear();
        m_streamUrl.clear();
    }

    setState(SessionState::Idle);
    LOG_INFO("Push session ended");
}

// ── Receiver side ────────────────────────────────────────────────────────

bool SessionManager::handlePlayRequest(const PlayRequest& req) {
    SessionState s = m_state;
    if (s != SessionState::Idle) {
        LOG_WARN("Rejecting play request: busy in state %s", sessionStateName(s));
        return false;
    }

    if (!m_config.autoAcceptPlayRequest) {
        LOG_INFO("Auto-accept disabled, but V1 always accepts");
    }

    setState(SessionState::Preparing);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sessionId = req.sessionId;
        m_targetDeviceId = req.sourceDeviceId;
        m_streamUrl = req.url;
    }

    // Start PullService
    if (!m_pullService->start(req.url, m_config.rtspTransport)) {
        setState(SessionState::Error, "PullService start failed");
        return false;
    }

    setState(SessionState::Receiving);
    LOG_INFO("Receiving stream: %s", req.url.c_str());
    return true;
}

void SessionManager::handleStopRequest() {
    SessionState s = m_state;
    if (s == SessionState::Receiving) {
        setState(SessionState::Stopping);
        m_pullService->stop();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_sessionId.clear();
            m_targetDeviceId.clear();
            m_streamUrl.clear();
        }
        setState(SessionState::Idle);
        LOG_INFO("Receive session ended by peer");
    }
}
