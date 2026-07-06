#pragma once

#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct DeviceInfo {
    std::string protocol   = "eshare";
    int         version    = 1;
    std::string deviceId;
    std::string deviceName = "E-Share-Device";
    std::string ip;
    uint16_t    controlPort = 17680;
    std::string status      = "idle";  // idle, pushing, receiving, error
    std::string activeSessionId;

    json toJson() const {
        json j;
        j["protocol"]        = protocol;
        j["version"]         = version;
        j["deviceId"]        = deviceId;
        j["deviceName"]      = deviceName;
        j["ip"]              = ip;
        j["controlPort"]     = controlPort;
        j["status"]          = status;
        j["activeSessionId"] = activeSessionId;
        return j;
    }

    static DeviceInfo fromJson(const json& j) {
        DeviceInfo d;
        d.protocol         = j.value("protocol", "eshare");
        d.version          = j.value("version", 1);
        d.deviceId         = j.value("deviceId", "");
        d.deviceName       = j.value("deviceName", "");
        d.ip               = j.value("ip", "");
        d.controlPort      = j.value("controlPort", 17680);
        d.status           = j.value("status", "idle");
        d.activeSessionId  = j.value("activeSessionId", "");
        return d;
    }
};

struct PlayRequest {
    std::string sessionId;
    std::string sourceDeviceId;
    std::string url;
    bool        fullscreen = false;
    bool        audio = true;

    json toJson() const {
        return {
            {"sessionId", sessionId},
            {"sourceDeviceId", sourceDeviceId},
            {"url", url},
            {"fullscreen", fullscreen},
            {"audio", audio}
        };
    }
};
