#pragma once
#include <array>
#include <cstdint>

// Strong channel ID type — prevents accidental arithmetic
enum class ChannelId : uint32_t {};

static constexpr uint32_t MAX_SAMPLES_PER_PACKET = 512;

// Lightweight per-sample structure used only on the client side.
// Does NOT depend on generator.h from the generator subdirectory.
struct ClientSample {
    uint64_t timestamp_ns;
    uint32_t channel_id;
    double   value;
};
