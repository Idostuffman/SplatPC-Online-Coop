#pragma once

#include "splat_mod_api.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>

namespace rbrew_mod_log {

using HostLogFn = void (*)(SplatModLogLevel level, const char* tag, const char* fmt, ...);

inline std::atomic<HostLogFn>& log_sink() {
    static std::atomic<HostLogFn> sink{nullptr};
    return sink;
}

inline void set_log_fn(HostLogFn fn) {
    log_sink().store(fn, std::memory_order_release);
}

inline void emit(SplatModLogLevel level, const char* tag, const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(buf, sizeof(buf), fmt ? fmt : "", ap);
    va_end(ap);
    if (n < 0) return;

    if (const HostLogFn sink = log_sink().load(std::memory_order_acquire)) {
        sink(level, tag ? tag : "online_coop", "%s", buf);
        return;
    }

    const char* prefix = "INFO";
    if (level == SPLATMOD_LOG_WARN)
        prefix = "WARN";
    else if (level == SPLATMOD_LOG_ERROR)
        prefix = "ERROR";
    else if (level == SPLATMOD_LOG_DEBUG)
        prefix = "DEBUG";
    std::fprintf(stderr, "[%s] [%s] %s", prefix, tag ? tag : "online_coop", buf);
}

} // namespace rbrew_mod_log

#define MOD_LOG_INFO(tag, ...) ::rbrew_mod_log::emit(SPLATMOD_LOG_INFO, tag, __VA_ARGS__)
#define MOD_LOG_WARN(tag, ...) ::rbrew_mod_log::emit(SPLATMOD_LOG_WARN, tag, __VA_ARGS__)
#define MOD_LOG_ERROR(tag, ...) ::rbrew_mod_log::emit(SPLATMOD_LOG_ERROR, tag, __VA_ARGS__)
#define MOD_LOG_DEBUG(tag, ...) ::rbrew_mod_log::emit(SPLATMOD_LOG_DEBUG, tag, __VA_ARGS__)
