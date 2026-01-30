#include "util/log.h"
#include <cstdio>
#include <cstdarg>

namespace mesh3d {

static LogLevel g_level = LogLevel::Info;

void log_set_level(LogLevel level) { g_level = level; }

void log_msg(LogLevel level, const char* fmt, ...) {
    if (level < g_level) return;

    const char* prefix = "";
    FILE* out = stdout;
    switch (level) {
        case LogLevel::Debug: prefix = "[DEBUG] "; break;
        case LogLevel::Info:  prefix = "[INFO]  "; break;
        case LogLevel::Warn:  prefix = "[WARN]  "; out = stderr; break;
        case LogLevel::Error: prefix = "[ERROR] "; out = stderr; break;
    }

    fprintf(out, "%s", prefix);
    va_list args;
    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);
    fprintf(out, "\n");
    fflush(out);
}

} // namespace mesh3d
