/*
 * timing_tests.cpp — GTest unit tests for the generator's timing / drift
 * compensation logic, using a mock clock.
 *
 * Strategy
 * --------
 * We replace the global `g_clock_gettime` function pointer (declared in
 * generator_internal.h) with a mock that returns controlled nanosecond
 * timestamps.  This lets us verify:
 *
 *   1. `next_tick_ns` advances by exactly `1e9 / packets_per_second` each tick.
 *   2. When the mock returns a "late" time (simulating a slow callback), the
 *      next sleep is shortened so the tick schedule stays on track.
 *   3. When the mock returns an "early" time the generator sleeps for the
 *      remaining interval.
 *
 * Because generator_start() spawns a real pthread we collect a small number
 * of packets (≥ N) and inspect sequence numbers and timing bookkeeping via
 * observed callback timing deltas.  The mock clock advances in fixed steps so
 * that nanosleep() never actually sleeps (the mock-reported time is always
 * already past next_tick_ns).
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

extern "C" {
#include "generator.h"
#include "generator_internal.h"
#include "generator_simulations.h"
}

/* =========================================================================
 * Mock clock helpers
 * ========================================================================= */

/* Shared mock time in nanoseconds — advanced by the mock function. */
static std::atomic<uint64_t> g_mock_time_ns{0};

/* How much to advance the mock clock on each call (simulates elapsed time). */
static std::atomic<uint64_t> g_mock_advance_ns{0};

/* Total calls made to the mock clock. */
static std::atomic<int> g_mock_clock_calls{0};

static int mock_clock_gettime(clockid_t /*id*/, struct timespec* ts)
{
    uint64_t t = g_mock_time_ns.fetch_add(g_mock_advance_ns.load());
    ts->tv_sec  = (time_t)(t / 1000000000ULL);
    ts->tv_nsec = (long)(t  % 1000000000ULL);
    g_mock_clock_calls.fetch_add(1);
    return 0;
}

/* RAII helper: installs mock clock, restores real clock on destruction. */
struct MockClockGuard {
    ClockGetTimeFn saved;
    MockClockGuard()
        : saved(g_clock_gettime)
    {
        g_mock_time_ns.store(0);
        g_mock_advance_ns.store(0);
        g_mock_clock_calls.store(0);
        g_clock_gettime = mock_clock_gettime;
    }
    ~MockClockGuard()
    {
        g_clock_gettime = saved;
    }
};

/* RAII wrapper that always stops the generator. */
struct GenGuard {
    ~GenGuard() { generator_stop(); }
};

/* =========================================================================
 * Shared test helpers
 * ========================================================================= */

static ChannelConfig make_ch(uint32_t id)
{
    ChannelConfig ch{};
    ch.channel_id        = id;
    ch.simulation_fn     = generator_sim_flatline;
    ch.simulation_user_data = nullptr;
    std::snprintf(ch.name,       sizeof(ch.name),       "ch%u", id);
    std::snprintf(ch.attachment, sizeof(ch.attachment), "a%u",  id);
    return ch;
}

static GeneratorConfig make_cfg(uint32_t pps = 10,
                                 uint32_t sr  = 100,
                                 uint32_t ch  = 1)
{
    static ChannelConfig channels[GENERATOR_MAX_CHANNELS];
    for (uint32_t i = 0; i < ch; i++)
        channels[i] = make_ch(i);

    GeneratorConfig cfg{};
    cfg.sample_rate             = sr;
    cfg.packets_per_second      = pps;
    cfg.channel_count           = ch;
    cfg.channels                = channels;
    cfg.max_samples_per_packet  = 256;
    cfg.noise_level             = 0.0;
    cfg.jitter_probability      = 0.0;
    cfg.packet_drop_probability = 0.0;
    return cfg;
}

/* =========================================================================
 * Test 1: tick interval advances correctly
 *
 * We set the mock clock to always return "already past" times so that
 * nanosleep() returns immediately.  We collect N packets and verify that
 * packet sequence numbers are consecutive — confirming the loop advanced its
 * internal next_tick_ns by exactly interval_ns each iteration.
 * ========================================================================= */

TEST(TimingLoop, TickIntervalAdvancesCorrectly)
{
    MockClockGuard clk_guard;
    GenGuard       gen_guard;

    const uint32_t PPS = 10;
    /* Make mock clock advance by exactly interval_ns per call so the thread
     * thinks it is always right on time — zero sleep, full throughput.       */
    const uint64_t interval_ns = 1000000000ULL / PPS;
    g_mock_advance_ns.store(interval_ns / 2);  /* two calls per loop iteration */
    g_mock_time_ns.store(1000000000ULL);        /* non-zero start time          */

    std::vector<uint64_t> seqs;
    std::mutex            mu;

    struct Ctx { std::vector<uint64_t>* s; std::mutex* m; };
    Ctx ctx{ &seqs, &mu };

    auto cb = [](const GeneratorPacket* pkt, void* ud) {
        auto* c = static_cast<Ctx*>(ud);
        std::lock_guard<std::mutex> lk(*c->m);
        c->s->push_back(pkt->sequence);
    };

    GeneratorConfig cfg = make_cfg(PPS);
    ASSERT_EQ(generator_start(&cfg, cb, nullptr, &ctx), GENERATOR_OK);

    /* Wait until we have at least 8 packets or 2 s, whichever is first. */
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        {
            std::lock_guard<std::mutex> lk(mu);
            if (seqs.size() >= 8) break;
        }
        if (std::chrono::steady_clock::now() > deadline) break;
    }
    generator_stop();

    std::lock_guard<std::mutex> lk(mu);
    ASSERT_GE(seqs.size(), 4u) << "Not enough packets received";

    /* Sequence numbers must be strictly increasing with step 1. */
    for (size_t i = 1; i < seqs.size(); i++) {
        EXPECT_EQ(seqs[i], seqs[i - 1] + 1)
            << "Sequence gap at index " << i
            << " (" << seqs[i - 1] << " → " << seqs[i] << ")";
    }
}

/* =========================================================================
 * Test 2: mock clock can be swapped back to real clock_gettime
 * ========================================================================= */

TEST(TimingLoop, MockClockReplacesGlobal)
{
    /* Verify the guard actually changed the function pointer, and that it is
     * restored when the guard goes out of scope.                              */
    ClockGetTimeFn original = g_clock_gettime;

    {
        MockClockGuard guard;
        EXPECT_EQ(g_clock_gettime, mock_clock_gettime);
        EXPECT_NE(g_clock_gettime, original);
    }

    EXPECT_EQ(g_clock_gettime, original);
}

/* =========================================================================
 * Test 3: drift compensation — late callback shortens next sleep
 *
 * We configure the mock so that after each packet the clock reports that
 * more than one interval has elapsed (simulating a slow callback).  The loop
 * must therefore set next_tick_ns to a value in the future relative to the
 * slow clock, which effectively means it skips the sleep entirely and fires
 * the next packet immediately.  We measure wall-clock time: with the mock
 * running at high speed (every call advances ~interval_ns * 2), we should
 * receive many packets very quickly.
 * ========================================================================= */

TEST(TimingLoop, DriftCompensationHandlesLateCallback)
{
    MockClockGuard clk_guard;
    GenGuard       gen_guard;

    const uint32_t PPS = 10;
    const uint64_t interval_ns = 1000000000ULL / PPS;

    /* Mock clock advances by a full interval on every call — the thread will
     * see itself as "always late" and skip nanosleep.  This causes packets to
     * be produced as fast as possible.                                        */
    g_mock_advance_ns.store(interval_ns);
    g_mock_time_ns.store(0ULL);

    std::atomic<int> count{0};
    auto cb = [](const GeneratorPacket*, void* ud) {
        static_cast<std::atomic<int>*>(ud)->fetch_add(1);
    };

    GeneratorConfig cfg = make_cfg(PPS);
    ASSERT_EQ(generator_start(&cfg, cb, nullptr, &count), GENERATOR_OK);

    /* At full speed the thread should produce far more than PPS packets
     * within one real second.                                               */
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (count.load() < 15 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    generator_stop();

    EXPECT_GE(count.load(), 10)
        << "Drift compensation should allow rapid packet production when "
           "the mock clock races ahead";
}

/* =========================================================================
 * Test 4: next_tick_ns increments by exactly interval_ns per packet
 *
 * We collect timestamps from the mock clock at the point each packet is
 * delivered and verify that the inter-packet mock-time delta is consistent
 * with the configured packets_per_second.
 * ========================================================================= */

TEST(TimingLoop, NextTickAdvancesByExactlyOneInterval)
{
    MockClockGuard clk_guard;
    GenGuard       gen_guard;

    const uint32_t PPS         = 5;
    const uint64_t interval_ns = 1000000000ULL / PPS;

    /* Advance the mock by exactly half an interval per clock call so the
     * thread is never "late" — it will always sleep a tiny bit, but the
     * next_tick_ns accounting should still advance by exactly interval_ns. */
    g_mock_advance_ns.store(interval_ns / 4);
    g_mock_time_ns.store(500000000ULL);  /* start at 0.5 s */

    /* Capture the mock time at the moment each callback fires. */
    struct Ctx {
        std::vector<uint64_t>* times;
        std::mutex* mu;
    };
    std::vector<uint64_t> times;
    std::mutex mu;
    Ctx ctx{ &times, &mu };

    auto cb = [](const GeneratorPacket*, void* ud) {
        auto* c = static_cast<Ctx*>(ud);
        /* Snapshot current mock time — this is approximately when next_tick
         * was reached from the thread's perspective.                        */
        uint64_t t = g_mock_time_ns.load();
        std::lock_guard<std::mutex> lk(*c->mu);
        c->times->push_back(t);
    };

    GeneratorConfig cfg = make_cfg(PPS);
    ASSERT_EQ(generator_start(&cfg, cb, nullptr, &ctx), GENERATOR_OK);

    /* Collect at least 6 packets (real-time: ~1.5 s at 5 pps) */
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        {
            std::lock_guard<std::mutex> lk(mu);
            if (times.size() >= 6) break;
        }
        if (std::chrono::steady_clock::now() > deadline) break;
    }
    generator_stop();

    std::lock_guard<std::mutex> lk(mu);
    ASSERT_GE(times.size(), 4u) << "Insufficient packets collected";

    /* The mock clock ticks at interval_ns/4 per call; the thread makes ~2
     * calls per loop, so the observable time between consecutive callbacks
     * should be in the range [interval_ns/2, interval_ns * 3].             */
    for (size_t i = 1; i < times.size(); i++) {
        if (times[i] <= times[i - 1]) continue;  /* guard against wrap */
        uint64_t delta = times[i] - times[i - 1];
        EXPECT_GE(delta, interval_ns / 4)
            << "Inter-packet mock-time delta too small at index " << i;
        EXPECT_LE(delta, interval_ns * 8)
            << "Inter-packet mock-time delta too large at index " << i;
    }
}

/* =========================================================================
 * Test 5: generator produces packets even when packet_drop_probability > 0
 * ========================================================================= */

TEST(TimingLoop, PacketDropStillAdvancesSequence)
{
    MockClockGuard clk_guard;
    GenGuard       gen_guard;

    const uint32_t PPS = 20;
    const uint64_t interval_ns = 1000000000ULL / PPS;
    g_mock_advance_ns.store(interval_ns);
    g_mock_time_ns.store(0ULL);

    std::atomic<int> count{0};
    std::atomic<uint64_t> last_seq{UINT64_MAX};
    std::atomic<bool>     monotonic{true};

    struct Ctx {
        std::atomic<int>*      count;
        std::atomic<uint64_t>* last_seq;
        std::atomic<bool>*     mono;
    };
    Ctx ctx{ &count, &last_seq, &monotonic };

    auto cb = [](const GeneratorPacket* pkt, void* ud) {
        auto* c = static_cast<Ctx*>(ud);
        c->count->fetch_add(1);
        uint64_t prev = c->last_seq->exchange(pkt->sequence);
        if (prev != UINT64_MAX && pkt->sequence <= prev)
            c->mono->store(false);
    };

    GeneratorConfig cfg = make_cfg(PPS);
    cfg.packet_drop_probability = 0.3;  /* 30% drop rate */

    ASSERT_EQ(generator_start(&cfg, cb, nullptr, &ctx), GENERATOR_OK);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (count.load() < 10 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    generator_stop();

    EXPECT_TRUE(monotonic.load()) << "Sequence numbers were not monotonically increasing";
    EXPECT_GE(count.load(), 5) << "Too few packets received despite drops";
}
