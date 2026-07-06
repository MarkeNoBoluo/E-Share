#include "config/ConfigLoader.h"
#include "common/Logger.h"
#include "control/DeviceInfo.h"
#include "control/DeviceDiscovery.h"
#include "control/ControlServer.h"
#include "control/ControlClient.h"
#include "session/SessionManager.h"
#include "cli/CliHandler.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include "network/NetworkUtil.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")

// ── UUID generation ──────────────────────────────────────────────────────
static std::string generateUUID() {
    GUID guid;
    CoCreateGuid(&guid);
    char buf[64];
    snprintf(buf, sizeof(buf),
        "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
        guid.Data1, guid.Data2, guid.Data3,
        guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
        guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return std::string(buf);
}

// Pick the best local IP for a given target (prefer same subnet)
static std::string getBestLocalIP(const std::string& targetIP = "") {
    auto ips = enumerateLocalIPs();
    if (ips.empty()) return "127.0.0.1";

    if (targetIP.empty()) return ips[0].ip;

    // Parse target IP into uint32
    IN_ADDR targetAddr;
    inet_pton(AF_INET, targetIP.c_str(), &targetAddr);
    ULONG targetIpNet = ntohl(targetAddr.S_un.S_addr);

    // Find local IP in same subnet as target
    for (auto& entry : ips) {
        if (entry.prefixLength == 0 || entry.prefixLength > 32) continue;
        ULONG mask = (entry.prefixLength == 0) ? 0 :
            (0xFFFFFFFF << (32 - entry.prefixLength));

        IN_ADDR localAddr;
        inet_pton(AF_INET, entry.ip.c_str(), &localAddr);
        ULONG localIpNet = ntohl(localAddr.S_un.S_addr);

        if ((localIpNet & mask) == (targetIpNet & mask)) {
            return entry.ip;
        }
    }

    // Fallback: first available IP
    return ips[0].ip;
}

static std::string getHostName() {
    char name[256];
    DWORD size = sizeof(name);
    if (GetComputerNameA(name, &size)) {
        return std::string(name);
    }
    return "Unknown";
}

int main(int argc, char* argv[]) {
    CoInitialize(nullptr);

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    Logger::instance().initFile("e-share.log");
    atexit([]() { Logger::instance().closeFile(); });

    LOG_INFO("========================================");
    LOG_INFO("  E-Share V1 starting...");
    LOG_INFO("========================================");

    // Load config
    const char* configPath = "config/e-share.json";
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            configPath = argv[++i];
        }
    }
    auto config = ConfigLoader::load(configPath);

    std::string localIP = getBestLocalIP();
    std::string hostname = getHostName();
    std::string deviceId = generateUUID();

    // Build self device info
    DeviceInfo selfInfo;
    selfInfo.deviceId    = deviceId;
    selfInfo.deviceName  = config.deviceName;
    selfInfo.ip          = localIP;
    selfInfo.controlPort = config.controlPort;
    selfInfo.status      = "idle";

    LOG_INFO("Device: %s [%s]", config.deviceName.c_str(), deviceId.c_str());
    LOG_INFO("Host:   %s", hostname.c_str());
    LOG_INFO("IP:     %s", localIP.c_str());
    LOG_INFO("Ports:  control=%u discovery=%u rtsp=%u",
        config.controlPort, config.discoveryPort, config.rtspPort);

    // Session manager
    SessionManager sessionMgr(config, localIP);
    std::mutex selfMutex;

    sessionMgr.setStateCallback([&](SessionState state, const std::string& detail) {
        std::lock_guard<std::mutex> lock(selfMutex);
        switch (state) {
            case SessionState::Idle:      selfInfo.status = "idle"; break;
            case SessionState::Preparing: selfInfo.status = "preparing"; break;
            case SessionState::Pushing:   selfInfo.status = "pushing"; break;
            case SessionState::Receiving: selfInfo.status = "receiving"; break;
            case SessionState::Stopping:  selfInfo.status = "stopping"; break;
            case SessionState::Error:     selfInfo.status = "error"; break;
        }
        printf("  [Status: %s] %s\n", sessionStateName(state),
            detail.empty() ? "" : detail.c_str());
    });

    // ControlServer
    ControlServer controlServer;
    ControlCallbacks cbs;

    cbs.onStatus = [&]() -> DeviceInfo {
        std::lock_guard<std::mutex> lock(selfMutex);
        DeviceInfo info = selfInfo;
        info.status = sessionStateName(sessionMgr.state());
        info.activeSessionId = sessionMgr.activeSessionId();
        return info;
    };

    cbs.onPlay = [&](const PlayRequest& pr) -> bool {
        return sessionMgr.handlePlayRequest(pr);
    };

    cbs.onStop = [&]() -> bool {
        sessionMgr.handleStopRequest();
        return true;
    };

    cbs.onPing = [&]() {};

    controlServer.start(config.controlPort, std::move(cbs));

    // DeviceDiscovery
    DeviceDiscovery discovery;
    discovery.start(config.discoveryPort, selfInfo, enumerateLocalIPs());

    // CLI Handler
    CliHandler cli;

    cli.setGetDevices([&]() { return discovery.getOnlineDevices(); });

    cli.setPush([&](const OnlineDevice& target) -> bool {
        return sessionMgr.startPush(target);
    });

    cli.setStop([&]() {
        SessionState s = sessionMgr.state();
        if (s == SessionState::Pushing || s == SessionState::Error) {
            sessionMgr.stopPush();
            printf("  Push session stopped.\n");
        } else if (s == SessionState::Receiving) {
            sessionMgr.handleStopRequest();
            printf("  Receive session stopped.\n");
        } else {
            printf("  No active session.\n");
        }
    });

    cli.setStatus([&]() {
        printf("  Device: %s [%s]\n", config.deviceName.c_str(), deviceId.c_str());
        printf("  IP:     %s\n", localIP.c_str());
        printf("  Status: %s\n", sessionStateName(sessionMgr.state()));

        std::string sid = sessionMgr.activeSessionId();
        if (!sid.empty()) {
            printf("  Session: %s\n", sid.c_str());
            auto tid = sessionMgr.targetDeviceId();
            if (!tid.empty()) printf("  Peer:    %s\n", tid.c_str());
            auto url = sessionMgr.streamUrl();
            if (!url.empty()) printf("  URL:     %s\n", url.c_str());
        }

        // Report process health
        SessionState s = sessionMgr.state();
        if (s == SessionState::Pushing || s == SessionState::Receiving) {
            bool healthy = sessionMgr.checkHealth();
            printf("  Processes: %s\n", healthy ? "healthy" : "ERROR — check log");
        }
        printf("  Online devices: %zu\n", discovery.getOnlineDevices().size());
    });

    // ── Main loop ───────────────────────────────────────────────────────
    printf("\n");
    LOG_INFO("E-Share V1 ready.");
    printf("Commands: list | push <id> | stop | status | quit\n\n");

    bool running = true;
    while (running) {
        printf("> ");
        fflush(stdout);

        char line[512];
        if (!fgets(line, sizeof(line), stdin)) break;

        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

        running = cli.execute(std::string(line));

        // Periodic health check after each command
        sessionMgr.checkHealth();
    }

    // ── Cleanup (ordered: pull → push → rtsp → control → discovery) ────
    LOG_INFO("Shutting down...");

    SessionState s = sessionMgr.state();
    if (s == SessionState::Pushing || s == SessionState::Error) {
        sessionMgr.stopPush();
    } else if (s == SessionState::Receiving) {
        sessionMgr.handleStopRequest();
    }

    discovery.stop();
    controlServer.stop();

    WSACleanup();
    CoUninitialize();
    LOG_INFO("E-Share stopped.");
    return 0;
}
