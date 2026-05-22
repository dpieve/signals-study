#pragma once

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <spdlog/spdlog.h>

/// Attempt to promote the calling thread to SCHED_FIFO priority 80.
/// Requires CAP_SYS_NICE or running as root; logs a warning on failure.
inline void set_thread_priority_realtime() noexcept {
    sched_param sp{};
    sp.sched_priority = 80;
    const int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    if (rc != 0) {
        spdlog::warn("set_thread_priority_realtime: pthread_setschedparam "
                     "failed (errno={}). Running at default priority.", rc);
    }
}

#else
/// No-op on non-Linux platforms.
inline void set_thread_priority_realtime() noexcept {}
#endif
