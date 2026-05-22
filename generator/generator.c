/*
 * generator.c — ICU Real-Time Telemetry Simulator: core generator implementation.
 *
 * Implements:
 *   - generator_config_validate()
 *   - generator_start()
 *   - generator_stop()
 *   - Internal timing loop (pthreads)
 *   - Gaussian noise via Box-Muller transform
 */

#define _POSIX_C_SOURCE 200809L

#include "generator.h"
#include "generator_internal.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>

/* -------------------------------------------------------------------------
 * Platform: set real-time scheduling on Linux, silently ignore on Windows.
 * ------------------------------------------------------------------------- */
#if defined(__linux__)
#  include <sched.h>
static void try_set_realtime_priority(void)
{
    struct sched_param sp;
    sp.sched_priority = sched_get_priority_min(SCHED_FIFO);
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    /* failure is silently ignored — not all platforms allow this */
}
#else
static void try_set_realtime_priority(void) { /* no-op on Windows/macOS */ }
#endif

/* -------------------------------------------------------------------------
 * Global clock function pointer — tests may replace this.
 * ------------------------------------------------------------------------- */
ClockGetTimeFn g_clock_gettime = clock_gettime;

/* -------------------------------------------------------------------------
 * Single global generator state.  Only one generator instance at a time.
 * ------------------------------------------------------------------------- */
static GeneratorState g_state;
static volatile int   g_initialized = 0;  /* 0 = idle, 1 = running */

/* -------------------------------------------------------------------------
 * Gaussian white noise via Box-Muller transform.
 * Returns a sample from N(0,1).
 * ------------------------------------------------------------------------- */
static double gaussian_noise(void)
{
    /* Box-Muller: produce two independent N(0,1) samples from two uniform
     * samples; cache the spare for the next call.                           */
    static int    have_spare = 0;
    static double spare;

    if (have_spare) {
        have_spare = 0;
        return spare;
    }

    double u, v, s;
    do {
        u = ((double)rand() / RAND_MAX) * 2.0 - 1.0;
        v = ((double)rand() / RAND_MAX) * 2.0 - 1.0;
        s = u * u + v * v;
    } while (s >= 1.0 || s <= 0.0);

    double mul = sqrt(-2.0 * log(s) / s);
    spare      = v * mul;
    have_spare = 1;
    return u * mul;
}

/* -------------------------------------------------------------------------
 * Helpers: timespec arithmetic
 * ------------------------------------------------------------------------- */
static uint64_t timespec_to_ns(const struct timespec* ts)
{
    return (uint64_t)ts->tv_sec * UINT64_C(1000000000) + (uint64_t)ts->tv_nsec;
}

static struct timespec ns_to_timespec(uint64_t ns)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(ns / UINT64_C(1000000000));
    ts.tv_nsec = (long)(ns % UINT64_C(1000000000));
    return ts;
}

/* -------------------------------------------------------------------------
 * Timing / generation thread
 * ------------------------------------------------------------------------- */
static void* timing_thread_fn(void* arg)
{
    GeneratorState* state = (GeneratorState*)arg;
    const GeneratorConfig* cfg = &state->config;

    try_set_realtime_priority();

    /* Interval between packets in nanoseconds */
    const uint64_t interval_ns = UINT64_C(1000000000) / (uint64_t)cfg->packets_per_second;

    /* samples_per_packet: how many time-steps each channel contributes per packet */
    const uint32_t samples_per_packet = cfg->sample_rate / cfg->packets_per_second;

    /* Inter-sample interval in nanoseconds */
    const uint64_t sample_interval_ns =
        (cfg->sample_rate > 0)
        ? UINT64_C(1000000000) / (uint64_t)cfg->sample_rate
        : interval_ns;

    /* Seed RNG (generator thread owns it) */
    srand((unsigned int)time(NULL));

    /* Get start time */
    struct timespec ts_now;
    g_clock_gettime(CLOCK_MONOTONIC, &ts_now);
    uint64_t next_tick_ns = timespec_to_ns(&ts_now) + interval_ns;
    uint64_t sample_base_ns = timespec_to_ns(&ts_now);

    uint64_t sequence    = 0;
    int      next_has_gap = 0;

    /* Pool index cycles through PACKET_POOL_SIZE entries */
    int pool_idx = 0;

    /* Notify caller that we have started */
    if (state->event_callback) {
        state->event_callback(GENERATOR_EVENT_STARTED, 0, "generator started",
                              state->user_data);
    }

    while (state->running) {
        /* ---- obtain a packet buffer from the pool ---- */
        GeneratorPacket* pkt = state->packet_pool[pool_idx];
        pool_idx = (pool_idx + 1) % PACKET_POOL_SIZE;

        pkt->sequence        = sequence++;
        pkt->sample_count    = 0;
        pkt->has_gap         = (uint32_t)next_has_gap;
        next_has_gap         = 0;

        /* ---- packet drop simulation ---- */
        if (cfg->packet_drop_probability > 0.0 &&
            ((double)rand() / RAND_MAX) < cfg->packet_drop_probability)
        {
            /* Drop this packet: bump sequence so the consumer sees a gap,
             * and flag the NEXT packet.                                      */
            next_has_gap = 1;
            /* still need to advance time */
            sample_base_ns += (uint64_t)samples_per_packet * sample_interval_ns;
            goto wait_next_tick;
        }

        /* ---- generate samples for all channels ---- */
        for (uint32_t ch = 0; ch < cfg->channel_count; ch++) {
            const ChannelConfig* channel = &state->channels[ch];

            uint32_t effective_samples = samples_per_packet;

            /* Jitter: randomly drop 1-3 samples from this channel's batch */
            if (cfg->jitter_probability > 0.0 &&
                ((double)rand() / RAND_MAX) < cfg->jitter_probability)
            {
                int drop = 1 + (rand() % 3);
                if ((int)effective_samples - drop > 0)
                    effective_samples -= (uint32_t)drop;
            }

            for (uint32_t s = 0; s < effective_samples; s++) {
                if (pkt->sample_count >= pkt->samples_capacity)
                    break;

                uint64_t ts_ns = sample_base_ns + (uint64_t)s * sample_interval_ns;

                double value = channel->simulation_fn(ts_ns, s,
                                                      channel->simulation_user_data);

                /* Apply additive Gaussian noise */
                if (cfg->noise_level > 0.0)
                    value += cfg->noise_level * gaussian_noise();

                pkt->samples[pkt->sample_count].timestamp_ns = ts_ns;
                pkt->samples[pkt->sample_count].channel_id   = channel->channel_id;
                pkt->samples[pkt->sample_count].value        = value;
                pkt->sample_count++;
            }
        }

        sample_base_ns += (uint64_t)samples_per_packet * sample_interval_ns;

        /* ---- deliver the packet ---- */
        state->packet_callback(pkt, state->user_data);

wait_next_tick:
        /* ---- drift-compensated sleep ---- */
        g_clock_gettime(CLOCK_MONOTONIC, &ts_now);
        uint64_t now_ns = timespec_to_ns(&ts_now);

        if (now_ns < next_tick_ns) {
            uint64_t sleep_ns = next_tick_ns - now_ns;
            struct timespec sleep_ts = ns_to_timespec(sleep_ns);
            nanosleep(&sleep_ts, NULL);
        }
        /* Advance next_tick_ns by one interval regardless of actual elapsed
         * time — this is the key drift-compensation step.                   */
        next_tick_ns += interval_ns;
    }

    if (state->event_callback) {
        state->event_callback(GENERATOR_EVENT_STOPPED, 0, "generator stopped",
                              state->user_data);
    }

    return NULL;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

GeneratorError generator_config_validate(const GeneratorConfig* config)
{
    if (!config)
        return GENERATOR_ERROR_INVALID_CONFIG;

    if (config->sample_rate == 0)
        return GENERATOR_ERROR_INVALID_CONFIG;

    if (config->packets_per_second == 0)
        return GENERATOR_ERROR_INVALID_CONFIG;

    if (config->channel_count == 0 || config->channel_count > GENERATOR_MAX_CHANNELS)
        return GENERATOR_ERROR_INVALID_CONFIG;

    if (!config->channels)
        return GENERATOR_ERROR_INVALID_CONFIG;

    for (uint32_t i = 0; i < config->channel_count; i++) {
        if (!config->channels[i].simulation_fn)
            return GENERATOR_ERROR_INVALID_CONFIG;
    }

    if (config->max_samples_per_packet == 0)
        return GENERATOR_ERROR_INVALID_CONFIG;

    return GENERATOR_OK;
}

GeneratorError generator_start(
    const GeneratorConfig*  config,
    GeneratorPacketCallback packet_callback,
    GeneratorEventCallback  event_callback,
    void*                   user_data)
{
    if (g_initialized)
        return GENERATOR_ERROR_ALREADY_RUNNING;

    GeneratorError err = generator_config_validate(config);
    if (err != GENERATOR_OK)
        return err;

    if (!packet_callback)
        return GENERATOR_ERROR_INVALID_CONFIG;

    /* Zero-initialise state */
    memset(&g_state, 0, sizeof(g_state));

    /* Copy config */
    g_state.config = *config;

    /* Copy channel configs into our local array so we own the memory */
    for (uint32_t i = 0; i < config->channel_count; i++)
        g_state.channels[i] = config->channels[i];
    g_state.config.channels = g_state.channels;

    g_state.packet_callback = packet_callback;
    g_state.event_callback  = event_callback;
    g_state.user_data       = user_data;

    /* Allocate packet pool */
    uint32_t capacity = config->max_samples_per_packet;
    for (int i = 0; i < PACKET_POOL_SIZE; i++) {
        size_t block_size = sizeof(GeneratorPacket)
                          + sizeof(GeneratorSample) * capacity;
        void* block = malloc(block_size);
        if (!block) {
            /* Free already-allocated blocks */
            for (int j = 0; j < i; j++) {
                free(g_state.packet_pool[j]);
                g_state.packet_pool[j] = NULL;
            }
            return GENERATOR_ERROR_THREAD_FAILED;
        }
        memset(block, 0, block_size);
        GeneratorPacket* pkt = (GeneratorPacket*)block;
        pkt->samples          = (GeneratorSample*)((char*)block + sizeof(GeneratorPacket));
        pkt->samples_capacity = capacity;
        g_state.packet_pool[i] = pkt;
    }

    g_state.running = 1;
    g_initialized   = 1;

    if (pthread_create(&g_state.timing_thread, NULL, timing_thread_fn, &g_state) != 0) {
        g_state.running = 0;
        g_initialized   = 0;
        for (int i = 0; i < PACKET_POOL_SIZE; i++) {
            free(g_state.packet_pool[i]);
            g_state.packet_pool[i] = NULL;
        }
        return GENERATOR_ERROR_THREAD_FAILED;
    }

    return GENERATOR_OK;
}

GeneratorError generator_stop(void)
{
    if (!g_initialized)
        return GENERATOR_OK;  /* idempotent */

    g_state.running = 0;
    pthread_join(g_state.timing_thread, NULL);

    /* Free packet pool */
    for (int i = 0; i < PACKET_POOL_SIZE; i++) {
        free(g_state.packet_pool[i]);
        g_state.packet_pool[i] = NULL;
    }

    g_initialized = 0;
    return GENERATOR_OK;
}
