#pragma once
#include <cstdio>
#include <cstdarg>

namespace mesh3d {

enum class LogLevel { Debug, Info, Warn, Error };

void log_set_level(LogLevel level);
void log_msg(LogLevel level, const char* fmt, ...);

} // namespace mesh3d

#define LOG_DEBUG(...) ::mesh3d::log_msg(::mesh3d::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...)  ::mesh3d::log_msg(::mesh3d::LogLevel::Info,  __VA_ARGS__)
#define LOG_WARN(...)  ::mesh3d::log_msg(::mesh3d::LogLevel::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) ::mesh3d::log_msg(::mesh3d::LogLevel::Error, __VA_ARGS__)
