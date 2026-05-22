#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <type_traits>
#include "../../generator/generator.h"

static constexpr uint32_t MAX_SAMPLES_PER_PACKET = 512;

static_assert(sizeof(GeneratorSample) % 8 == 0, "GeneratorSample must be 8-byte aligned");

enum class ChannelId : uint32_t {};

struct IngestedPacket {
    uint64_t sequence;
    uint64_t first_timestamp_ns;
    uint32_t sample_count;
    uint32_t has_gap;
    std::array<GeneratorSample, MAX_SAMPLES_PER_PACKET> samples;
};
static_assert(std::is_trivially_copyable_v<IngestedPacket>);

struct CommittedBatch {
    std::vector<IngestedPacket> packets;
    uint64_t commit_sequence;
};
