#include "media/PullService.h"
#include "common/Logger.h"
#include <cstdio>

PullService::PullService() {}

PullService::~PullService() {
    stop();
}

bool PullService::start(const std::string& url, const std::string& transport,
    const std::string& binPath) {
    if (m_running) {
        LOG_WARN("PullService already running");
        return true;
    }

    // Resolve RTSP-Player.exe path (same pattern as RtspServerManager)
    char resolvedExe[MAX_PATH];
    DWORD attr = GetFileAttributesA(binPath.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        GetFullPathNameA(binPath.c_str(), MAX_PATH, resolvedExe, nullptr);
    } else {
        char selfPath[MAX_PATH];
        GetModuleFileNameA(NULL, selfPath, MAX_PATH);
        char* lastSep = strrchr(selfPath, '\\');
        if (lastSep) *lastSep = '\0';
        snprintf(resolvedExe, sizeof(resolvedExe), "%s\\RTSP-Player.exe", selfPath);
    }

    char cmdLine[2048];
    snprintf(cmdLine, sizeof(cmdLine),
        "\"%s\""
        " --url \"%s\""
        " --transport %s"
        " --setpts-zero"
        " --log rtsp_player_e-share.log",
        resolvedExe,
        url.c_str(),
        transport.c_str());

    LOG_INFO("PullService starting: %s", cmdLine);

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
    si.wShowWindow = SW_SHOW;  // Player window should be visible
    si.hStdInput = childStdinRead;
    si.hStdOutput = childStdoutWrite;
    si.hStdError = childStderrWrite;

    if (!CreateProcessA(nullptr, cmdLine, nullptr, nullptr, TRUE,
        CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &m_pi)) {
        LOG_ERROR("PullService: CreateProcess failed (error %lu)", GetLastError());
        CloseHandle(childStdinRead);
        CloseHandle(m_stdinWrite);
        CloseHandle(m_stdoutRead);
        CloseHandle(childStdoutWrite);
        CloseHandle(m_stderrRead);
        CloseHandle(childStderrWrite);
        return false;
    }

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
    LOG_INFO("PullService started (PID %lu)", m_pi.dwProcessId);
    return true;
}

void PullService::stop() {
    if (!m_running.exchange(false)) return;

    LOG_INFO("PullService: stopping...");

    // Send 'q' via stdin for graceful exit
    if (m_stdinWrite) {
        const char quitCmd[] = "q\n";
        DWORD written = 0;
        WriteFile(m_stdinWrite, quitCmd, static_cast<DWORD>(sizeof(quitCmd) - 1), &written, nullptr);
        CloseHandle(m_stdinWrite);
        m_stdinWrite = nullptr;
    }

    DWORD wait = WaitForSingleObject(m_pi.hProcess, 5000);
    if (wait != WAIT_OBJECT_0) {
        LOG_WARN("PullService: timeout, force-terminating");
        TerminateProcess(m_pi.hProcess, 0);
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(m_pi.hProcess, &exitCode);
    LOG_INFO("PullService stopped (exit code %lu)", exitCode);

    CloseHandle(m_pi.hProcess);
    CloseHandle(m_pi.hThread);

    // Join drain threads before closing pipe handles
    if (m_stdoutThread.joinable()) m_stdoutThread.join();
    if (m_stderrThread.joinable()) m_stderrThread.join();

    if (m_stdoutRead) { CloseHandle(m_stdoutRead); m_stdoutRead = nullptr; }
    if (m_stderrRead) { CloseHandle(m_stderrRead); m_stderrRead = nullptr; }

    LOG_INFO("PullService stopped");
}

bool PullService::checkAlive() const {
    if (!m_running) return false;
    DWORD code;
    if (!GetExitCodeProcess(m_pi.hProcess, &code)) return false;
    if (code != STILL_ACTIVE) {
        const_cast<PullService*>(this)->m_exitCode = code;
        return false;
    }
    return true;
}

DWORD PullService::exitCode() const {
    return m_exitCode;
}
