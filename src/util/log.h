#pragma once
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>
#include <mutex>

namespace mesh3d {

enum class LogLevel { Debug, Info, Warn, Error };

void log_set_level(LogLevel level);
void log_msg(LogLevel level, const char* fmt, ...);

/* Ring buffer of recent log messages for HUD display */
struct LogEntry {
    LogLevel level;
    std::string text;
};

std::vector<LogEntry> log_recent(int max_count = 3);

} // namespace mesh3d

#define LOG_DEBUG(...) ::mesh3d::log_msg(::mesh3d::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...)  ::mesh3d::log_msg(::mesh3d::LogLevel::Info,  __VA_ARGS__)
#define LOG_WARN(...)  ::mesh3d::log_msg(::mesh3d::LogLevel::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) ::mesh3d::log_msg(::mesh3d::LogLevel::Error, __VA_ARGS__)
