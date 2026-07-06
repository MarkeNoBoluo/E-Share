#include "control/DeviceDiscovery.h"
#include "common/Logger.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>

DeviceDiscovery::DeviceDiscovery() = default;

DeviceDiscovery::~DeviceDiscovery() {
    stop();
}

bool DeviceDiscovery::start(uint16_t port, const DeviceInfo& selfInfo, const std::vector<LocalIPEntry>& localIPs) {
    m_selfInfo = selfInfo;
    m_localIPs = localIPs;
    m_port = port;

    m_socket = static_cast<int>(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (m_socket < 0) {
        LOG_ERROR("DeviceDiscovery: failed to create socket");
        return false;
    }

    // Enable broadcast
    int broadcast = 1;
    setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST,
        reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));

    // Enable address reuse
    int reuse = 1;
    setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    // Bind
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_ERROR("DeviceDiscovery: bind failed on port %u", port);
        closesocket(m_socket);
        m_socket = -1;
        return false;
    }

    // Set non-blocking for receive
    u_long mode = 1;
    ioctlsocket(m_socket, FIONBIO, &mode);

    m_running = true;
    m_broadcastThread = std::thread(&DeviceDiscovery::broadcastLoop, this);
    m_receiveThread = std::thread(&DeviceDiscovery::receiveLoop, this);
    m_cleanupThread = std::thread(&DeviceDiscovery::cleanupLoop, this);

    LOG_INFO("DeviceDiscovery started on UDP %u", port);
    return true;
}

void DeviceDiscovery::stop() {
    if (!m_running.exchange(false)) return;

    if (m_socket >= 0) {
        closesocket(m_socket);
        m_socket = -1;
    }

    if (m_broadcastThread.joinable()) m_broadcastThread.join();
    if (m_receiveThread.joinable()) m_receiveThread.join();
    if (m_cleanupThread.joinable()) m_cleanupThread.join();

    LOG_INFO("DeviceDiscovery stopped");
}

std::vector<OnlineDevice> DeviceDiscovery::getOnlineDevices() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<OnlineDevice> result;
    result.reserve(m_devices.size());
    for (auto& pair : m_devices) {
        result.push_back(pair.second);
    }
    return result;
}

void DeviceDiscovery::broadcastLoop() {
    sockaddr_in broadcastAddr{};
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(m_port);

    const char* discoverMsg = "ESHARE_DISCOVER";

    while (m_running) {
        // Broadcast to every connected subnet
        for (auto& entry : m_localIPs) {
            if (entry.prefixLength == 0 || entry.prefixLength > 32) continue;

            // Compute subnet-directed broadcast: ip | ~netmask
            IN_ADDR addr;
            inet_pton(AF_INET, entry.ip.c_str(), &addr);
            ULONG ipNet = ntohl(addr.S_un.S_addr);
            ULONG mask = 0xFFFFFFFF << (32 - entry.prefixLength);
            ULONG broadcast = ipNet | ~mask;

            broadcastAddr.sin_addr.s_addr = htonl(broadcast);

            sendto(m_socket, discoverMsg, static_cast<int>(strlen(discoverMsg)), 0,
                reinterpret_cast<sockaddr*>(&broadcastAddr), sizeof(broadcastAddr));
        }

        for (int i = 0; i < 50 && m_running; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void DeviceDiscovery::receiveLoop() {
    char buf[4096];

    while (m_running) {
        sockaddr_in fromAddr{};
        int fromLen = sizeof(fromAddr);
        int len = recvfrom(m_socket, buf, sizeof(buf) - 1, 0,
            reinterpret_cast<sockaddr*>(&fromAddr), &fromLen);

        if (len < 0) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            continue;
        }

        buf[len] = '\0';
        std::string msg(buf, len);

        char fromIP[64];
        inet_ntop(AF_INET, &fromAddr.sin_addr, fromIP, sizeof(fromIP));

        if (msg == "ESHARE_DISCOVER") {
            // Another device is searching — reply with our info
            std::string reply = m_selfInfo.toJson().dump();
            sendto(m_socket, reply.c_str(), static_cast<int>(reply.size()), 0,
                reinterpret_cast<const sockaddr*>(&fromAddr), sizeof(fromAddr));
        } else {
            // Try parsing as JSON (ESHARE_HERE reply containing DeviceInfo)
            try {
                json j = json::parse(msg);
                if (j.value("protocol", "") == "eshare") {
                    DeviceInfo info = DeviceInfo::fromJson(j);
                    info.ip = fromIP;  // Use actual sender IP, not self-reported
                    std::string id = info.deviceId;

                    // Skip our own device
                    if (id == m_selfInfo.deviceId) continue;

                    std::lock_guard<std::mutex> lock(m_mutex);
                    auto it = m_devices.find(id);
                    if (it != m_devices.end()) {
                        it->second.info = std::move(info);
                        it->second.lastSeen = std::chrono::steady_clock::now();
                    } else {
                        OnlineDevice dev;
                        dev.info = std::move(info);
                        dev.lastSeen = std::chrono::steady_clock::now();
                        m_devices[id] = std::move(dev);
                        LOG_INFO("New device discovered: %s [%s] @ %s",
                            dev.info.deviceName.c_str(), id.c_str(),
                            dev.info.ip.c_str());
                    }
                }
            } catch (...) {
                // Not JSON — ignore
            }
        }
    }
}

void DeviceDiscovery::cleanupLoop() {
    while (m_running) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_devices.begin();
        while (it != m_devices.end()) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(
                now - it->second.lastSeen).count();
            if (age > 15) {
                LOG_INFO("Device offline: %s [%s]",
                    it->second.info.deviceName.c_str(),
                    it->second.info.deviceId.c_str());
                it = m_devices.erase(it);
            } else {
                ++it;
            }
        }
    }
}
