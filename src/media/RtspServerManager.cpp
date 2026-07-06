#include "media/RtspServerManager.h"
#include "common/Logger.h"
#include <ws2tcpip.h>
#include <cstdio>

RtspServerManager::RtspServerManager() {}

RtspServerManager::~RtspServerManager() {
    stop();
}

bool RtspServerManager::start(uint16_t port, const std::string& binPath) {
    if (m_running) {
        LOG_WARN("RtspServer already running");
        return true;
    }

    // Resolve mediamtx.exe path
    char resolvedExe[MAX_PATH];
    DWORD attr = GetFileAttributesA(binPath.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        // binPath points to an existing file — resolve it
        GetFullPathNameA(binPath.c_str(), MAX_PATH, resolvedExe, nullptr);
    } else {
        // Fall back to exe directory (same dir as E-Share.exe)
        char selfPath[MAX_PATH];
        GetModuleFileNameA(NULL, selfPath, MAX_PATH);
        char* lastSep = strrchr(selfPath, '\\');
        if (lastSep) *lastSep = '\0';
        snprintf(resolvedExe, sizeof(resolvedExe), "%s\\mediamtx.exe", selfPath);
    }

    // Derive yml path from resolved exe directory
    char ymlPath[MAX_PATH];
    char exeDir[MAX_PATH];
    strncpy(exeDir, resolvedExe, MAX_PATH);
    char* lastSep = strrchr(exeDir, '\\');
    if (lastSep) *lastSep = '\0';
    snprintf(ymlPath, sizeof(ymlPath), "%s\\mediamtx.yml", exeDir);

    // Ensure directory exists
    CreateDirectoryA(exeDir, NULL);

    FILE* f = fopen(ymlPath, "w");
    if (!f) {
        LOG_ERROR("Cannot write MediaMTX config: %s", ymlPath);
        return false;
    }
    fprintf(f,
        "rtspAddress: :%u\n"
        "rtmpAddress: :1935\n"
        "hlsAddress: :8888\n"
        "webrtcAddress: :8889\n"
        "paths:\n"
        "  all:\n"
        "    source: publisher\n"
        "    sourceOnDemand: no\n",
        port);
    fclose(f);

    // Start MediaMTX with the config file
    char cmdLine[1024];
    snprintf(cmdLine, sizeof(cmdLine), "\"%s\" \"%s\"", resolvedExe, ymlPath);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessA(nullptr, cmdLine, nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &m_pi)) {
        LOG_ERROR("Failed to start MediaMTX: %s (error %lu)", resolvedExe, GetLastError());
        return false;
    }

    // Wait for the RTSP port to become available
    LOG_INFO("Waiting for MediaMTX on port %u...", port);
    if (!waitForPort(port, 10000)) {
        LOG_ERROR("MediaMTX did not bind port %u within timeout", port);
        TerminateProcess(m_pi.hProcess, 1);
        CloseHandle(m_pi.hProcess);
        CloseHandle(m_pi.hThread);
        return false;
    }

    m_running = true;
    LOG_INFO("MediaMTX started on port %u", port);
    return true;
}

void RtspServerManager::stop() {
    if (!m_running.exchange(false)) return;

    LOG_INFO("Stopping MediaMTX...");
    // Try graceful first — send WM_CLOSE
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == static_cast<DWORD>(lParam)) {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
            return FALSE;
        }
        return TRUE;
    }, m_pi.dwProcessId);

    // Wait up to 3 seconds for graceful exit
    DWORD wait = WaitForSingleObject(m_pi.hProcess, 3000);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(m_pi.hProcess, 0);
        LOG_INFO("MediaMTX force-terminated");
    }

    CloseHandle(m_pi.hProcess);
    CloseHandle(m_pi.hThread);
    LOG_INFO("MediaMTX stopped");
}

bool RtspServerManager::waitForPort(uint16_t port, int timeoutMs) {
    int elapsed = 0;
    const int step = 200;

    while (elapsed < timeoutMs) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            closesocket(s);
            return true;
        }

        closesocket(s);
        Sleep(step);
        elapsed += step;
    }

    return false;
}
