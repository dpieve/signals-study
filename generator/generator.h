#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GENERATOR_MAX_CHANNELS 12

typedef enum GeneratorError {
    GENERATOR_OK = 0,
    GENERATOR_ERROR_INVALID_CONFIG,
    GENERATOR_ERROR_ALREADY_RUNNING,
    GENERATOR_ERROR_THREAD_FAILED
} GeneratorError;

typedef enum GeneratorEventType {
    GENERATOR_EVENT_STARTED,
    GENERATOR_EVENT_STOPPED,
    GENERATOR_EVENT_FAILED
} GeneratorEventType;

typedef struct GeneratorSample {
    uint64_t timestamp_ns;
    uint32_t channel_id;
    double   value;
} GeneratorSample;

typedef struct GeneratorPacket {
    uint64_t  sequence;
    uint32_t  sample_count;
    uint32_t  samples_capacity;
    uint32_t  has_gap;
    GeneratorSample* samples;
} GeneratorPacket;

typedef double (*GeneratorSimulationFn)(
    uint64_t timestamp_ns,
    uint32_t sample_index,
    void* simulation_user_data);

typedef struct ChannelConfig {
    uint32_t channel_id;
    char name[64];
    char attachment[64];
    GeneratorSimulationFn simulation_fn;
    void* simulation_user_data;
} ChannelConfig;

typedef struct GeneratorConfig {
    uint32_t sample_rate;
    uint32_t packets_per_second;
    uint32_t            channel_count;
    const ChannelConfig* channels;
    uint32_t max_samples_per_packet;
    double noise_level;
    double jitter_probability;
    double packet_drop_probability;
} GeneratorConfig;

typedef void (*GeneratorPacketCallback)(
    const GeneratorPacket* packet,
    void* user_data);

typedef void (*GeneratorEventCallback)(
    GeneratorEventType event,
    int                error_code,
    const char*        message,
    void*              user_data);

GeneratorError generator_config_validate(const GeneratorConfig* config);

GeneratorError generator_start(
    const GeneratorConfig*  config,
    GeneratorPacketCallback packet_callback,
    GeneratorEventCallback  event_callback,
    void*                   user_data);

GeneratorError generator_stop(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
