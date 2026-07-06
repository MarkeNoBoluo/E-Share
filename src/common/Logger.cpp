#include "common/Logger.h"
#include <cstdarg>
#include <ctime>
#include <chrono>
#include <windows.h>

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

Logger::~Logger() {
    closeFile();
}

void Logger::initFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file) fclose(m_file);
    m_file = fopen(path.c_str(), "a");
}

void Logger::closeFile() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file) {
        fclose(m_file);
        m_file = nullptr;
    }
}

static const char* levelStr(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

static int levelColor(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return 8;   // gray
        case LogLevel::Info:  return 7;   // white
        case LogLevel::Warn:  return 14;  // yellow
        case LogLevel::Error: return 12;  // red
    }
    return 7;
}

void Logger::log(LogLevel level, const char* file, int line, const char* fmt, ...) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    char timeBuf[32];
    std::tm tm;
    localtime_s(&tm, &time);
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tm);

    // Extract filename from path
    const char* basename = file;
    const char* p = file;
    while (*p) {
        if (*p == '\\' || *p == '/') basename = p + 1;
        p++;
    }

    char msgBuf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);

    // Console output with color
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(console, &csbi);
    WORD defaultColor = csbi.wAttributes;

    std::lock_guard<std::mutex> lock(m_mutex);

    SetConsoleTextAttribute(console, levelColor(level));
    fprintf(stdout, "[%s.%03lld] [%s] [%s:%d] %s\n",
        timeBuf, ms.count(), levelStr(level), basename, line, msgBuf);
    fflush(stdout);
    SetConsoleTextAttribute(console, defaultColor);

    // File output
    if (m_file) {
        fprintf(m_file, "[%s.%03lld] [%s] [%s:%d] %s\n",
            timeBuf, ms.count(), levelStr(level), basename, line, msgBuf);
        fflush(m_file);
    }
}
