#pragma once

#include "control/DeviceInfo.h"
#include "network/NetworkUtil.h"
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <chrono>

struct OnlineDevice {
    DeviceInfo  info;
    std::chrono::steady_clock::time_point lastSeen;

    std::string key() const { return info.deviceId; }
};

class DeviceDiscovery {
public:
    DeviceDiscovery();
    ~DeviceDiscovery();

    bool start(uint16_t port, const DeviceInfo& selfInfo, const std::vector<LocalIPEntry>& localIPs);
    void stop();

    // Thread-safe: returns copy of current online devices
    std::vector<OnlineDevice> getOnlineDevices();

private:
    void broadcastLoop();
    void receiveLoop();
    void cleanupLoop();

    DeviceInfo m_selfInfo;
    std::vector<LocalIPEntry> m_localIPs;
    uint16_t m_port = 0;
    int m_socket = -1;

    std::atomic<bool> m_running{false};
    std::thread m_broadcastThread;
    std::thread m_receiveThread;
    std::thread m_cleanupThread;

    std::mutex m_mutex;
    std::unordered_map<std::string, OnlineDevice> m_devices;
};
