#include "cli/CliHandler.h"
#include "common/Logger.h"
#include <cstdio>
#include <cstring>

CliHandler::CliHandler() = default;

bool CliHandler::execute(const std::string& line) {
    if (line.empty()) return true;

    if (line == "quit") {
        return false;
    } else if (line == "list") {
        cmdList();
    } else if (line.rfind("push ", 0) == 0) {
        cmdPush(line.substr(5));
    } else if (line == "stop") {
        cmdStop();
    } else if (line == "status") {
        cmdStatus();
    } else {
        printf("  Unknown command. Available: list | push <id> | stop | status | quit\n");
    }
    return true;
}

void CliHandler::cmdList() {
    if (!m_getDevices) return;
    auto devices = m_getDevices();
    if (devices.empty()) {
        printf("  No devices online.\n");
        return;
    }
    printf("  %-36s %-20s %-16s %s\n", "Device ID", "Name", "IP", "Status");
    printf("  %-36s %-20s %-16s %s\n",
        "----------------------------------", "--------------------",
        "----------------", "--------");
    for (auto& d : devices) {
        printf("  %-36s %-20s %-16s %s\n",
            d.info.deviceId.c_str(), d.info.deviceName.c_str(),
            d.info.ip.c_str(), d.info.status.c_str());
    }
}

void CliHandler::cmdPush(const std::string& targetId) {
    if (!m_push || !m_getDevices) return;

    auto devices = m_getDevices();
    for (auto& d : devices) {
        if (d.info.deviceId == targetId) {
            printf("  Pushing to %s @ %s:%u ...\n",
                d.info.deviceName.c_str(), d.info.ip.c_str(),
                d.info.controlPort);
            if (m_push(d)) {
                printf("  Push session started.\n");
            } else {
                printf("  Push failed — check logs for details.\n");
            }
            return;
        }
    }
    printf("  Device not found: %s\n", targetId.c_str());
}

void CliHandler::cmdStop() {
    if (m_stop) m_stop();
}

void CliHandler::cmdStatus() {
    if (m_status) m_status();
}
