#pragma once

#include "control/DeviceInfo.h"
#include <string>

struct ControlClientResult {
    bool        ok = false;
    int         httpStatus = 0;
    std::string body;
};

class ControlClient {
public:
    // Check target device status
    static ControlClientResult getStatus(const std::string& ip, uint16_t port);

    // Request target device to play a stream
    static ControlClientResult sendPlay(const std::string& ip, uint16_t port,
        const std::string& sessionId, const std::string& sourceDeviceId,
        const std::string& url, bool fullscreen, bool audio);

    // Request target device to stop playing
    static ControlClientResult sendStop(const std::string& ip, uint16_t port);

    // Ping target device
    static ControlClientResult sendPing(const std::string& ip, uint16_t port);
};
