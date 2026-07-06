#include "config/ConfigLoader.h"
#include "common/Logger.h"
#include <fstream>
#include <windows.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static void save(const std::string& path, const EShareConfig& config) {
    // Ensure parent directory exists
    std::string dir = path.substr(0, path.find_last_of("\\/"));
    if (!dir.empty()) {
        CreateDirectoryA(dir.c_str(), nullptr);
    }

    json j;
    j["deviceName"] = config.deviceName;
    j["controlPort"] = config.controlPort;
    j["discoveryPort"] = config.discoveryPort;
    j["rtspPort"] = config.rtspPort;
    j["defaultOutputSize"] = config.defaultOutputSize;
    j["defaultFps"] = config.defaultFps;
    j["defaultBitrateKbps"] = config.defaultBitrateKbps;
    j["rtspTransport"] = config.rtspTransport;
    j["autoAcceptPlayRequest"] = config.autoAcceptPlayRequest;

    std::ofstream file(path);
    if (file.is_open()) {
        file << j.dump(4);
        LOG_INFO("Default config written to %s", path.c_str());
    } else {
        LOG_WARN("Failed to write default config to %s", path.c_str());
    }
}

EShareConfig ConfigLoader::defaults() {
    return EShareConfig{};
}

EShareConfig ConfigLoader::load(const std::string& path) {
    EShareConfig config = defaults();

    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARN("Config file not found: %s, generating defaults", path.c_str());
        save(path, config);
        return config;
    }

    try {
        json j;
        file >> j;

        if (j.contains("deviceName"))       config.deviceName = j["deviceName"].get<std::string>();
        if (j.contains("controlPort"))       config.controlPort = j["controlPort"].get<uint16_t>();
        if (j.contains("discoveryPort"))     config.discoveryPort = j["discoveryPort"].get<uint16_t>();
        if (j.contains("rtspPort"))          config.rtspPort = j["rtspPort"].get<uint16_t>();
        if (j.contains("defaultOutputSize")) config.defaultOutputSize = j["defaultOutputSize"].get<std::string>();
        if (j.contains("defaultFps"))        config.defaultFps = j["defaultFps"].get<int>();
        if (j.contains("defaultBitrateKbps")) config.defaultBitrateKbps = j["defaultBitrateKbps"].get<int>();
        if (j.contains("rtspTransport"))     config.rtspTransport = j["rtspTransport"].get<std::string>();
        if (j.contains("autoAcceptPlayRequest")) config.autoAcceptPlayRequest = j["autoAcceptPlayRequest"].get<bool>();

        LOG_INFO("Config loaded from %s", path.c_str());
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse config: %s, using defaults", e.what());
    }

    return config;
}
