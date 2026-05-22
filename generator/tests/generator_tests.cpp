/*
 * generator_tests.cpp — GTest unit tests for the core generator API.
 *
 * Tests:
 *   - generator_config_validate() with valid and invalid configs
 *   - Callback is called with correct sample count
 *   - Sequence monotonicity across multiple packets
 *   - Packet sizing: sample_count == (sample_rate / packets_per_second) * channel_count
 *   - generator_stop() properly halts the generator
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <cstring>

extern "C" {
#include "generator.h"
#include "generator_simulations.h"
}

/* =========================================================================
 * Helpers
 * ========================================================================= */

/* A minimal valid config used as a baseline in multiple tests. */
static ChannelConfig make_flatline_channel(uint32_t id)
{
    ChannelConfig ch{};
    ch.channel_id       = id;
    ch.simulation_fn    = generator_sim_flatline;
    ch.simulation_user_data = nullptr;
    std::snprintf(ch.name,       sizeof(ch.name),       "ch%u", id);
    std::snprintf(ch.attachment, sizeof(ch.attachment), "loc%u", id);
    return ch;
}

static GeneratorConfig make_valid_config(uint32_t ch_count = 1)
{
    static ChannelConfig channels[GENERATOR_MAX_CHANNELS];
    for (uint32_t i = 0; i < ch_count; i++)
        channels[i] = make_flatline_channel(i);

    GeneratorConfig cfg{};
    cfg.sample_rate            = 100;   /* Hz */
    cfg.packets_per_second     = 10;
    cfg.channel_count          = ch_count;
    cfg.channels               = channels;
    cfg.max_samples_per_packet = 256;
    cfg.noise_level            = 0.0;
    cfg.jitter_probability     = 0.0;
    cfg.packet_drop_probability = 0.0;
    return cfg;
}

/* RAII wrapper: always calls generator_stop() on destruction. */
struct GeneratorGuard {
    ~GeneratorGuard() { generator_stop(); }
};

/* =========================================================================
 * generator_config_validate — valid configurations
 * ========================================================================= */

TEST(GeneratorConfigValidate, ValidSingleChannel)
{
    GeneratorConfig cfg = make_valid_config(1);
    EXPECT_EQ(generator_config_validate(&cfg), GENERATOR_OK);
}

TEST(GeneratorConfigValidate, ValidMaxChannels)
{
    GeneratorConfig cfg = make_valid_config(GENERATOR_MAX_CHANNELS);
    EXPECT_EQ(generator_config_validate(&cfg), GENERATOR_OK);
}

/* =========================================================================
 * generator_config_validate — invalid configurations
 * ========================================================================= */

TEST(GeneratorConfigValidate, NullPointer)
{
    EXPECT_EQ(generator_config_validate(nullptr), GENERATOR_ERROR_INVALID_CONFIG);
}

TEST(GeneratorConfigValidate, ZeroSampleRate)
{
    GeneratorConfig cfg = make_valid_config(1);
    cfg.sample_rate = 0;
    EXPECT_EQ(generator_config_validate(&cfg), GENERATOR_ERROR_INVALID_CONFIG);
}

TEST(GeneratorConfigValidate, ZeroPacketsPerSecond)
{
    GeneratorConfig cfg = make_valid_config(1);
    cfg.packets_per_second = 0;
    EXPECT_EQ(generator_config_validate(&cfg), GENERATOR_ERROR_INVALID_CONFIG);
}

TEST(GeneratorConfigValidate, ZeroChannelCount)
{
    GeneratorConfig cfg = make_valid_config(1);
    cfg.channel_count = 0;
    EXPECT_EQ(generator_config_validate(&cfg), GENERATOR_ERROR_INVALID_CONFIG);
}

TEST(GeneratorConfigValidate, ChannelCountExceedsMax)
{
    GeneratorConfig cfg = make_valid_config(1);
    cfg.channel_count = GENERATOR_MAX_CHANNELS + 1;
    EXPECT_EQ(generator_config_validate(&cfg), GENERATOR_ERROR_INVALID_CONFIG);
}

TEST(GeneratorConfigValidate, NullChannelsPointer)
{
    GeneratorConfig cfg = make_valid_config(1);
    cfg.channels = nullptr;
    EXPECT_EQ(generator_config_validate(&cfg), GENERATOR_ERROR_INVALID_CONFIG);
}

TEST(GeneratorConfigValidate, NullSimulationFn)
{
    static ChannelConfig ch = make_flatline_channel(0);
    ch.simulation_fn = nullptr;  /* intentionally broken */

    GeneratorConfig cfg = make_valid_config(1);
    cfg.channels = &ch;
    EXPECT_EQ(generator_config_validate(&cfg), GENERATOR_ERROR_INVALID_CONFIG);
}

TEST(GeneratorConfigValidate, ZeroMaxSamplesPerPacket)
{
    GeneratorConfig cfg = make_valid_config(1);
    cfg.max_samples_per_packet = 0;
    EXPECT_EQ(generator_config_validate(&cfg), GENERATOR_ERROR_INVALID_CONFIG);
}

/* =========================================================================
 * generator_start / generator_stop — basic lifecycle
 * ========================================================================= */

TEST(GeneratorLifecycle, StartAndStop)
{
    GeneratorGuard guard;
    GeneratorConfig cfg = make_valid_config(1);

    GeneratorError err = generator_start(
        &cfg,
        [](const GeneratorPacket*, void*) {},
        nullptr,
        nullptr);

    EXPECT_EQ(err, GENERATOR_OK);
    EXPECT_EQ(generator_stop(), GENERATOR_OK);
}

TEST(GeneratorLifecycle, DoubleStartReturnsError)
{
    GeneratorGuard guard;
    GeneratorConfig cfg = make_valid_config(1);

    generator_start(&cfg, [](const GeneratorPacket*, void*) {}, nullptr, nullptr);

    GeneratorError second = generator_start(
        &cfg,
        [](const GeneratorPacket*, void*) {},
        nullptr,
        nullptr);

    EXPECT_EQ(second, GENERATOR_ERROR_ALREADY_RUNNING);
}

TEST(GeneratorLifecycle, StopWithoutStartIsOk)
{
    /* Should be idempotent and not crash. */
    EXPECT_EQ(generator_stop(), GENERATOR_OK);
}

/* =========================================================================
 * Callback invocation — sample count correctness
 * ========================================================================= */

TEST(GeneratorCallback, CallbackInvokedWithCorrectSampleCount)
{
    GeneratorGuard guard;

    const uint32_t SAMPLE_RATE  = 100;
    const uint32_t PPS          = 10;
    const uint32_t CH           = 2;
    const uint32_t EXPECTED_SAMPLES_PER_PACKET =
        (SAMPLE_RATE / PPS) * CH;  /* 10 * 2 = 20 */

    std::atomic<int> mismatch_count{0};
    std::atomic<int> packet_count{0};

    GeneratorConfig cfg = make_valid_config(CH);
    cfg.sample_rate        = SAMPLE_RATE;
    cfg.packets_per_second = PPS;
    cfg.max_samples_per_packet = 256;
    cfg.noise_level        = 0.0;
    cfg.jitter_probability = 0.0;
    cfg.packet_drop_probability = 0.0;

    auto cb = [](const GeneratorPacket* pkt, void* ud) {
        auto* ctx = reinterpret_cast<
            std::pair<std::atomic<int>*, std::atomic<int>*>*>(ud);
        ctx->second->fetch_add(1);
        if (pkt->sample_count != 20u)
            ctx->first->fetch_add(1);
    };

    auto ctx = std::make_pair(&mismatch_count, &packet_count);

    generator_start(&cfg, cb, nullptr, &ctx);

    /* Collect at least 5 packets (≥ 500 ms at 10 pps) */
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(700);
    while (packet_count.load() < 5 &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    generator_stop();

    (void)EXPECTED_SAMPLES_PER_PACKET;
    EXPECT_GE(packet_count.load(), 5);
    EXPECT_EQ(mismatch_count.load(), 0)
        << "One or more packets had unexpected sample count";
}

/* =========================================================================
 * Sequence monotonicity
 * ========================================================================= */

TEST(GeneratorCallback, SequenceMonotonicallyIncreases)
{
    GeneratorGuard guard;

    std::vector<uint64_t> sequences;
    std::mutex            mu;

    GeneratorConfig cfg = make_valid_config(1);
    cfg.sample_rate        = 100;
    cfg.packets_per_second = 10;
    cfg.max_samples_per_packet = 64;
    cfg.packet_drop_probability = 0.0;

    struct Ctx { std::vector<uint64_t>* seqs; std::mutex* mu; };
    Ctx ctx{ &sequences, &mu };

    auto cb = [](const GeneratorPacket* pkt, void* ud) {
        auto* c = static_cast<Ctx*>(ud);
        std::lock_guard<std::mutex> lk(*c->mu);
        c->seqs->push_back(pkt->sequence);
    };

    generator_start(&cfg, cb, nullptr, &ctx);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::lock_guard<std::mutex> lk(mu);
        if (sequences.size() >= 5) break;
        if (std::chrono::steady_clock::now() > deadline) break;
    }
    generator_stop();

    ASSERT_GE(sequences.size(), 5u);
    for (size_t i = 1; i < sequences.size(); i++) {
        EXPECT_GT(sequences[i], sequences[i - 1])
            << "Sequence not monotonically increasing at index " << i;
    }
}

/* =========================================================================
 * Packet sizing
 * ========================================================================= */

TEST(GeneratorCallback, PacketSizingMatchesSampleRateDivPacketsPerSecond)
{
    GeneratorGuard guard;

    const uint32_t SR  = 500;
    const uint32_t PPS = 25;
    const uint32_t CH  = 1;
    const uint32_t EXPECTED = (SR / PPS) * CH;  /* 20 samples */

    std::atomic<bool> wrong_size{false};
    std::atomic<int>  count{0};

    struct Ctx { uint32_t expected; std::atomic<bool>* wrong; std::atomic<int>* count; };
    Ctx ctx{ EXPECTED, &wrong_size, &count };

    GeneratorConfig cfg = make_valid_config(CH);
    cfg.sample_rate        = SR;
    cfg.packets_per_second = PPS;
    cfg.max_samples_per_packet = 256;
    cfg.noise_level        = 0.0;
    cfg.jitter_probability = 0.0;
    cfg.packet_drop_probability = 0.0;

    auto cb = [](const GeneratorPacket* pkt, void* ud) {
        auto* c = static_cast<Ctx*>(ud);
        if (pkt->sample_count != c->expected)
            c->wrong->store(true);
        c->count->fetch_add(1);
    };

    generator_start(&cfg, cb, nullptr, &ctx);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (count.load() < 5 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    generator_stop();

    EXPECT_FALSE(wrong_size.load())
        << "At least one packet had wrong sample_count (expected " << EXPECTED << ")";
    EXPECT_GE(count.load(), 5);
}

/* =========================================================================
 * generator_stop() halts packet delivery
 * ========================================================================= */

TEST(GeneratorLifecycle, StopHaltsCallbacks)
{
    std::atomic<int> count_before{0};
    std::atomic<int> count_after{0};
    std::atomic<bool> stopped{false};

    struct Ctx {
        std::atomic<int>*  before;
        std::atomic<int>*  after;
        std::atomic<bool>* stopped;
    };
    Ctx ctx{ &count_before, &count_after, &stopped };

    GeneratorConfig cfg = make_valid_config(1);
    cfg.sample_rate        = 100;
    cfg.packets_per_second = 20;
    cfg.max_samples_per_packet = 64;

    auto cb = [](const GeneratorPacket*, void* ud) {
        auto* c = static_cast<Ctx*>(ud);
        if (!c->stopped->load())
            c->before->fetch_add(1);
        else
            c->after->fetch_add(1);
    };

    generator_start(&cfg, cb, nullptr, &ctx);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    stopped.store(true);
    generator_stop();  /* joins the thread */

    int after_val = count_after.load();

    /* Give a brief window to confirm no more callbacks arrive */
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_EQ(count_after.load(), after_val)
        << "Callbacks continued after generator_stop()";
    EXPECT_GT(count_before.load(), 0)
        << "No callbacks received before stop";
}
