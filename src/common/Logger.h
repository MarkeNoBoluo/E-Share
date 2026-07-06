#pragma once

#include <string>
#include <cstdio>
#include <mutex>

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error
};

class Logger {
public:
    static Logger& instance();

    void initFile(const std::string& path);
    void closeFile();
    void log(LogLevel level, const char* file, int line, const char* fmt, ...);

private:
    Logger() = default;
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    FILE* m_file = nullptr;
    std::mutex m_mutex;
};

#define LOG_DEBUG(fmt, ...) Logger::instance().log(LogLevel::Debug, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  Logger::instance().log(LogLevel::Info,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  Logger::instance().log(LogLevel::Warn,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) Logger::instance().log(LogLevel::Error, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
