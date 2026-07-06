#include "control/ControlServer.h"
#include "common/Logger.h"
#include <httplib.h>

ControlServer::ControlServer() = default;

ControlServer::~ControlServer() {
    stop();
}

bool ControlServer::start(uint16_t port, ControlCallbacks callbacks) {
    m_port = port;
    m_callbacks = std::move(callbacks);
    m_svr = std::make_unique<httplib::Server>();
    m_running = true;
    m_thread = std::thread(&ControlServer::serveLoop, this);
    LOG_INFO("ControlServer started on TCP %u", port);
    return true;
}

void ControlServer::stop() {
    if (!m_running.exchange(false)) return;
    m_svr->stop();
    if (m_thread.joinable()) m_thread.join();
    m_svr.reset();
    LOG_INFO("ControlServer stopped");
}

void ControlServer::setStatusCallback(std::function<DeviceInfo()> cb) {
    m_callbacks.onStatus = std::move(cb);
}

void ControlServer::serveLoop() {
    httplib::Server& svr = *m_svr;

    svr.Get("/status", [this](const httplib::Request&, httplib::Response& res) {
        if (m_callbacks.onStatus) {
            DeviceInfo info = m_callbacks.onStatus();
            res.set_content(info.toJson().dump(), "application/json");
        } else {
            res.status = 500;
            res.set_content(R"({"error":"no status callback"})", "application/json");
        }
    });

    svr.Post("/session/play", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json j = json::parse(req.body);
            PlayRequest pr;
            pr.sessionId      = j.value("sessionId", "");
            pr.sourceDeviceId = j.value("sourceDeviceId", "");
            pr.url            = j.value("url", "");
            pr.fullscreen     = j.value("fullscreen", false);
            pr.audio          = j.value("audio", true);

            LOG_INFO("Received play request: session=%s from=%s url=%s",
                pr.sessionId.c_str(), pr.sourceDeviceId.c_str(), pr.url.c_str());

            if (m_callbacks.onPlay && m_callbacks.onPlay(pr)) {
                res.set_content(R"({"accepted":true})", "application/json");
            } else {
                res.status = 409;
                res.set_content(R"({"accepted":false,"reason":"busy or rejected"})",
                    "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string(R"({"error":")") + e.what() + "\"}", "application/json");
        }
    });

    svr.Post("/session/stop", [this](const httplib::Request&, httplib::Response& res) {
        LOG_INFO("Received stop request");
        if (m_callbacks.onStop && m_callbacks.onStop()) {
            res.set_content(R"({"stopped":true})", "application/json");
        } else {
            res.set_content(R"({"stopped":true})", "application/json");  // Ack always
        }
    });

    svr.Post("/session/ping", [this](const httplib::Request&, httplib::Response& res) {
        if (m_callbacks.onPing) m_callbacks.onPing();
        res.set_content(R"({"pong":true})", "application/json");
    });

    svr.listen("0.0.0.0", m_port);
}
