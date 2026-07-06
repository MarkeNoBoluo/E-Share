#include "control/ControlClient.h"
#include "common/Logger.h"
#include <httplib.h>

static ControlClientResult doRequest(
    const std::string& ip, uint16_t port,
    const std::string& method, const std::string& path,
    const std::string& body = "") {

    ControlClientResult result;
    httplib::Client cli(ip, port);
    cli.set_connection_timeout(3, 0);
    cli.set_read_timeout(5, 0);

    httplib::Result res;
    if (method == "GET") {
        res = cli.Get(path.c_str());
    } else if (method == "POST") {
        res = cli.Post(path.c_str(),
            body.empty() ? httplib::Headers{} : httplib::Headers{},
            body, "application/json");
    }

    if (!res) {
        result.ok = false;
        result.httpStatus = 0;
        result.body = "connection failed";
        return result;
    }

    result.ok = (res->status >= 200 && res->status < 300);
    result.httpStatus = res->status;
    result.body = res->body;
    return result;
}

ControlClientResult ControlClient::getStatus(const std::string& ip, uint16_t port) {
    return doRequest(ip, port, "GET", "/status");
}

ControlClientResult ControlClient::sendPlay(const std::string& ip, uint16_t port,
    const std::string& sessionId, const std::string& sourceDeviceId,
    const std::string& url, bool fullscreen, bool audio) {
    PlayRequest pr;
    pr.sessionId = sessionId;
    pr.sourceDeviceId = sourceDeviceId;
    pr.url = url;
    pr.fullscreen = fullscreen;
    pr.audio = audio;
    return doRequest(ip, port, "POST", "/session/play", pr.toJson().dump());
}

ControlClientResult ControlClient::sendStop(const std::string& ip, uint16_t port) {
    return doRequest(ip, port, "POST", "/session/stop");
}

ControlClientResult ControlClient::sendPing(const std::string& ip, uint16_t port) {
    return doRequest(ip, port, "POST", "/session/ping");
}
