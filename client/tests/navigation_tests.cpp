#include <gtest/gtest.h>
#include "../timeline/timeline_controller.h"
#include <cstdint>

// ---------------------------------------------------------------------------
// Historical scroll bounds
// ---------------------------------------------------------------------------

TEST(NavigationTests, CannotScrollBeforeEpoch) {
    TimelineController ctrl;
    ctrl.on_new_sample(10'000'000'000ULL);
    ctrl.set_historical(100'000'000ULL); // 0.1 s

    ctrl.scroll_by(-999'999'999'999LL); // huge negative
    EXPECT_EQ(ctrl.state().current_ts_ns, 0u);
}

TEST(NavigationTests, CannotScrollPastLiveHead) {
    TimelineController ctrl;
    constexpr uint64_t live = 60ULL * 1'000'000'000ULL; // 60 s
    ctrl.on_new_sample(live);
    ctrl.set_historical(live / 2);

    ctrl.scroll_by(static_cast<int64_t>(live * 10)); // 10x past live head
    EXPECT_EQ(ctrl.state().current_ts_ns, live);
}

TEST(NavigationTests, ScrollForwardAndBackwardReturnToOrigin) {
    TimelineController ctrl;
    ctrl.on_new_sample(20'000'000'000ULL);
    ctrl.set_historical(5'000'000'000ULL);

    ctrl.scroll_by(2'000'000'000LL);  // +2 s  → 7 s
    ctrl.scroll_by(-2'000'000'000LL); // -2 s  → 5 s again
    EXPECT_EQ(ctrl.state().current_ts_ns, 5'000'000'000ULL);
}

// ---------------------------------------------------------------------------
// Jump-to-timestamp accuracy
// ---------------------------------------------------------------------------

TEST(NavigationTests, SetHistoricalLandsExactlyOnTimestamp) {
    TimelineController ctrl;
    ctrl.on_new_sample(100'000'000'000ULL);

    constexpr uint64_t target = 42'123'456'789ULL;
    ctrl.set_historical(target);

    EXPECT_EQ(ctrl.state().current_ts_ns, target);
}

TEST(NavigationTests, MultipleJumpsAreAccurate) {
    TimelineController ctrl;
    ctrl.on_new_sample(100'000'000'000ULL);

    const uint64_t timestamps[] = {
        10'000'000'000ULL,
        50'000'000'000ULL,
        99'999'999'999ULL,
        1ULL,
        0ULL
    };
    for (uint64_t ts : timestamps) {
        ctrl.set_historical(ts);
        EXPECT_EQ(ctrl.state().current_ts_ns, ts) << "Failed for ts=" << ts;
    }
}

// ---------------------------------------------------------------------------
// Mode transitions during navigation
// ---------------------------------------------------------------------------

TEST(NavigationTests, SwitchingToLiveResetsCursorToLatest) {
    TimelineController ctrl;
    constexpr uint64_t latest = 30'000'000'000ULL;
    ctrl.on_new_sample(latest);

    ctrl.set_historical(5'000'000'000ULL);
    EXPECT_EQ(ctrl.state().mode, PlaybackMode::Historical);

    ctrl.set_live();
    EXPECT_EQ(ctrl.state().mode, PlaybackMode::Live);
    EXPECT_EQ(ctrl.state().current_ts_ns, latest);
}

TEST(NavigationTests, ScrollingInLiveModeHasNoEffect) {
    TimelineController ctrl;
    ctrl.on_new_sample(50'000'000'000ULL);
    EXPECT_EQ(ctrl.state().mode, PlaybackMode::Live);

    ctrl.scroll_by(10'000'000'000LL);
    EXPECT_EQ(ctrl.state().mode, PlaybackMode::Live);
    EXPECT_EQ(ctrl.state().current_ts_ns, 50'000'000'000ULL);
}

TEST(NavigationTests, LiveHeadAdvancesAsNewSamplesArrive) {
    TimelineController ctrl;

    for (uint64_t t = 0; t <= 10; ++t) {
        ctrl.on_new_sample(t * 1'000'000'000ULL);
    }

    EXPECT_EQ(ctrl.state().current_ts_ns, 10'000'000'000ULL);
}

// ---------------------------------------------------------------------------
// Historical scroll with fine granularity (sub-millisecond)
// ---------------------------------------------------------------------------

TEST(NavigationTests, SubMillisecondScrollPrecision) {
    TimelineController ctrl;
    ctrl.on_new_sample(1'000'000'000'000ULL); // 1000 s
    ctrl.set_historical(500'000'000'000ULL);  // 500 s

    ctrl.scroll_by(1LL);   // +1 ns
    EXPECT_EQ(ctrl.state().current_ts_ns, 500'000'000'001ULL);

    ctrl.scroll_by(-1LL);  // back
    EXPECT_EQ(ctrl.state().current_ts_ns, 500'000'000'000ULL);
}
