#pragma once
#include <assert.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include "generator.h"

/* Ensure channel count fits in a uint64_t bitmask */
#ifdef __cplusplus
static_assert(GENERATOR_MAX_CHANNELS <= 64, "channel count must fit in a uint64_t bitmask");
#else
_Static_assert(GENERATOR_MAX_CHANNELS <= 64, "channel count must fit in a uint64_t bitmask");
#endif

/* Packet pool size: two packets per channel gives enough double-buffering headroom */
#define PACKET_POOL_SIZE (GENERATOR_MAX_CHANNELS * 2)

/* Clock function pointer type — default is clock_gettime, replaceable in tests */
typedef int (*ClockGetTimeFn)(clockid_t, struct timespec*);
extern ClockGetTimeFn g_clock_gettime;

/*
 * Internal state held for the duration of a generator run.
 * The packet pool is allocated as a flat block:
 *   malloc(sizeof(GeneratorPacket) + sizeof(GeneratorSample) * max_samples_per_packet)
 * The `samples` pointer inside each GeneratorPacket is set to point at the
 * memory immediately following the struct.
 */
typedef struct GeneratorState {
    pthread_t       timing_thread;
    volatile int    running;
    GeneratorConfig config;                        /* deep copy of caller config  */
    ChannelConfig   channels[GENERATOR_MAX_CHANNELS]; /* local channel config copy */

    /* Packet pool — PACKET_POOL_SIZE individually malloc'd blocks              */
    GeneratorPacket* packet_pool[PACKET_POOL_SIZE];

    /* Callbacks and user data */
    GeneratorPacketCallback packet_callback;
    GeneratorEventCallback  event_callback;
    void*                   user_data;
} GeneratorState;
