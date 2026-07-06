#pragma once

#include <string>
#include <cstdint>

struct EShareConfig {
    std::string deviceName = "E-Share-Device";
    uint16_t    controlPort = 17680;
    uint16_t    discoveryPort = 17679;
    uint16_t    rtspPort = 8554;
    std::string defaultOutputSize = "1920x1080";
    int         defaultFps = 30;
    int         defaultBitrateKbps = 8000;
    std::string rtspTransport = "tcp";
    bool        autoAcceptPlayRequest = true;
};

class ConfigLoader {
public:
    static EShareConfig load(const std::string& path);
    static EShareConfig defaults();
};
