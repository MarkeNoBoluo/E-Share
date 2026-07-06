#include "media/PushService.h"
#include "common/Logger.h"
#include <cstdio>

PushService::PushService() {}

PushService::~PushService() {
    stop();
}

bool PushService::start(const PushConfig& config, const std::string& binPath) {
    if (m_running) {
        LOG_WARN("PushService already running");
        return true;
    }

    // Resolve RTSP-Pusher.exe path (same pattern as RtspServerManager)
    char resolvedExe[MAX_PATH];
    DWORD attr = GetFileAttributesA(binPath.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        GetFullPathNameA(binPath.c_str(), MAX_PATH, resolvedExe, nullptr);
    } else {
        char selfPath[MAX_PATH];
        GetModuleFileNameA(NULL, selfPath, MAX_PATH);
        char* lastSep = strrchr(selfPath, '\\');
        if (lastSep) *lastSep = '\0';
        snprintf(resolvedExe, sizeof(resolvedExe), "%s\\RTSP-Pusher.exe", selfPath);
    }

    // Build command line
    char cmdLine[2048];
    snprintf(cmdLine, sizeof(cmdLine),
        "\"%s\""
        " --url \"%s\""
        " --screen %d"
        " --output-size %s"
        " --fps %d"
        " --bitrate %d"
        " --transport %s"
        " --log rtsp_pusher_e-share.log"
        " --no-audio",
        resolvedExe,
        config.url.c_str(),
        config.screenIndex,
        config.outputSize.c_str(),
        config.fps,
        config.bitrateKbps,
        config.transport.c_str());

    LOG_INFO("PushService starting: %s", cmdLine);

    // Create pipes for stdin/stdout/stderr
    HANDLE childStdinRead = nullptr;
    HANDLE childStdoutWrite = nullptr;
    HANDLE childStderrWrite = nullptr;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    CreatePipe(&childStdinRead, &m_stdinWrite, &sa, 0);
    SetHandleInformation(m_stdinWrite, HANDLE_FLAG_INHERIT, 0);

    CreatePipe(&m_stdoutRead, &childStdoutWrite, &sa, 0);
    SetHandleInformation(m_stdoutRead, HANDLE_FLAG_INHERIT, 0);

    CreatePipe(&m_stderrRead, &childStderrWrite, &sa, 0);
    SetHandleInformation(m_stderrRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = childStdinRead;
    si.hStdOutput = childStdoutWrite;
    si.hStdError = childStderrWrite;

    if (!CreateProcessA(nullptr, cmdLine, nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &m_pi)) {
        LOG_ERROR("PushService: CreateProcess failed (error %lu)", GetLastError());
        CloseHandle(childStdinRead);
        CloseHandle(m_stdinWrite);
        CloseHandle(m_stdoutRead);
        CloseHandle(childStdoutWrite);
        CloseHandle(m_stderrRead);
        CloseHandle(childStderrWrite);
        return false;
    }

    // Close child-side handles (parent doesn't need them)
    CloseHandle(childStdinRead);
    CloseHandle(childStdoutWrite);
    CloseHandle(childStderrWrite);

    // Drain stdout/stderr pipes to prevent child process blocking on full buffer
    m_stdoutThread = std::thread([this]() {
        char buf[256];
        DWORD read;
        while (ReadFile(m_stdoutRead, buf, sizeof(buf), &read, nullptr) && read > 0) {}
    });
    m_stderrThread = std::thread([this]() {
        char buf[256];
        DWORD read;
        while (ReadFile(m_stderrRead, buf, sizeof(buf), &read, nullptr) && read > 0) {}
    });

    m_running = true;
    LOG_INFO("PushService started (PID %lu)", m_pi.dwProcessId);
    return true;
}

void PushService::stop() {
    if (!m_running.exchange(false)) return;

    LOG_INFO("PushService: stopping...");

    // Send 'q' via stdin for graceful exit
    const char quitCmd[] = "q\n";
    DWORD written = 0;
    WriteFile(m_stdinWrite, quitCmd, static_cast<DWORD>(sizeof(quitCmd) - 1), &written, nullptr);
    CloseHandle(m_stdinWrite);
    m_stdinWrite = nullptr;

    // Wait up to 5 seconds for the process to exit gracefully
    DWORD wait = WaitForSingleObject(m_pi.hProcess, 5000);
    if (wait != WAIT_OBJECT_0) {
        LOG_WARN("PushService: timeout, force-terminating");
        TerminateProcess(m_pi.hProcess, 0);
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(m_pi.hProcess, &exitCode);
    LOG_INFO("PushService stopped (exit code %lu)", exitCode);

    CloseHandle(m_pi.hProcess);
    CloseHandle(m_pi.hThread);

    // Join drain threads before closing pipe handles
    if (m_stdoutThread.joinable()) m_stdoutThread.join();
    if (m_stderrThread.joinable()) m_stderrThread.join();

    // Close read handles
    if (m_stdoutRead) {
        CloseHandle(m_stdoutRead);
        m_stdoutRead = nullptr;
    }
    if (m_stderrRead) {
        CloseHandle(m_stderrRead);
        m_stderrRead = nullptr;
    }

    LOG_INFO("PushService stopped");
}

bool PushService::checkAlive() const {
    if (!m_running) return false;
    DWORD code;
    if (!GetExitCodeProcess(m_pi.hProcess, &code)) return false;
    if (code != STILL_ACTIVE) {
        // Process exited — store exit code
        const_cast<PushService*>(this)->m_exitCode = code;
        return false;
    }
    return true;
}

DWORD PushService::exitCode() const {
    return m_exitCode;
}
