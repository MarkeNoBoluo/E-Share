#pragma once

#include "session/SessionManager.h"
#include "control/DeviceDiscovery.h"
#include <functional>
#include <string>

class CliHandler {
public:
    using GetDevicesFunc = std::function<std::vector<OnlineDevice>()>;
    using PushFunc = std::function<bool(const OnlineDevice&)>;
    using StopFunc = std::function<void()>;
    using StatusFunc = std::function<void()>;

    CliHandler();

    // Set callbacks for CLI actions
    void setGetDevices(GetDevicesFunc f)  { m_getDevices = std::move(f); }
    void setPush(PushFunc f)              { m_push = std::move(f); }
    void setStop(StopFunc f)              { m_stop = std::move(f); }
    void setStatus(StatusFunc f)          { m_status = std::move(f); }

    // Process one command line. Returns false if "quit" was entered.
    bool execute(const std::string& line);

private:
    void cmdList();
    void cmdPush(const std::string& targetId);
    void cmdStop();
    void cmdStatus();

    GetDevicesFunc m_getDevices;
    PushFunc m_push;
    StopFunc m_stop;
    StatusFunc m_status;
};
