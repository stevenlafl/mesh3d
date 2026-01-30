#include "util/log.h"
#include <cstdio>
#include <cstdarg>
#include <array>

namespace mesh3d {

static LogLevel g_level = LogLevel::Info;

/* Ring buffer for recent messages */
static constexpr int LOG_RING_SIZE = 16;
static std::array<LogEntry, LOG_RING_SIZE> g_ring;
static int g_ring_head = 0;  // next write position
static int g_ring_count = 0;
static std::mutex g_ring_mutex;

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

    /* Format the message */
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    /* Print to console */
    fprintf(out, "%s%s\n", prefix, buf);
    fflush(out);

    /* Store in ring buffer */
    {
        std::lock_guard<std::mutex> lock(g_ring_mutex);
        g_ring[g_ring_head] = {level, std::string(buf)};
        g_ring_head = (g_ring_head + 1) % LOG_RING_SIZE;
        if (g_ring_count < LOG_RING_SIZE) g_ring_count++;
    }
}

std::vector<LogEntry> log_recent(int max_count) {
    std::lock_guard<std::mutex> lock(g_ring_mutex);
    int count = std::min(max_count, g_ring_count);
    std::vector<LogEntry> result;
    result.reserve(count);
    /* Read oldest-first so newest is at the bottom */
    int start = (g_ring_head - count + LOG_RING_SIZE) % LOG_RING_SIZE;
    for (int i = 0; i < count; ++i) {
        int idx = (start + i) % LOG_RING_SIZE;
        result.push_back(g_ring[idx]);
    }
    return result;
}

} // namespace mesh3d
