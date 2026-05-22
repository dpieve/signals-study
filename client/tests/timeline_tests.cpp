#include <gtest/gtest.h>
#include "../timeline/timeline_controller.h"

// ---------------------------------------------------------------------------
// Initial state
// ---------------------------------------------------------------------------

TEST(TimelineController, InitialStateIsLive) {
    TimelineController ctrl;
    EXPECT_EQ(ctrl.state().mode, PlaybackMode::Live);
    EXPECT_EQ(ctrl.state().current_ts_ns, 0u);
    EXPECT_DOUBLE_EQ(ctrl.state().playback_speed, 1.0);
}

// ---------------------------------------------------------------------------
// set_live / set_historical transitions
// ---------------------------------------------------------------------------

TEST(TimelineController, SetHistoricalSwitchesMode) {
    TimelineController ctrl;
    ctrl.set_historical(123'000'000'000ULL);
    EXPECT_EQ(ctrl.state().mode, PlaybackMode::Historical);
    EXPECT_EQ(ctrl.state().current_ts_ns, 123'000'000'000ULL);
}

TEST(TimelineController, SetLiveRestoresLiveMode) {
    TimelineController ctrl;
    ctrl.set_historical(500'000'000ULL);
    EXPECT_EQ(ctrl.state().mode, PlaybackMode::Historical);

    ctrl.set_live();
    EXPECT_EQ(ctrl.state().mode, PlaybackMode::Live);
}

TEST(TimelineController, SetLiveUsesLatestTimestamp) {
    TimelineController ctrl;
    ctrl.on_new_sample(1'000'000'000ULL);
    ctrl.set_historical(0ULL); // go historical
    ctrl.set_live();           // return to live
    EXPECT_EQ(ctrl.state().current_ts_ns, 1'000'000'000ULL);
}

// ---------------------------------------------------------------------------
// scroll_by
// ---------------------------------------------------------------------------

TEST(TimelineController, ScrollByDoesNothingInLiveMode) {
    TimelineController ctrl;
    ctrl.on_new_sample(5'000'000'000ULL);
    // In live mode, cursor == latest_ts_ns
    const uint64_t before = ctrl.state().current_ts_ns;
    ctrl.scroll_by(1'000'000'000LL);
    EXPECT_EQ(ctrl.state().current_ts_ns, before);
}

TEST(TimelineController, ScrollByAdvancesCursorInHistoricalMode) {
    TimelineController ctrl;
    ctrl.on_new_sample(10'000'000'000ULL);
    ctrl.set_historical(1'000'000'000ULL);

    ctrl.scroll_by(500'000'000LL); // +0.5 s
    EXPECT_EQ(ctrl.state().current_ts_ns, 1'500'000'000ULL);
}

TEST(TimelineController, ScrollByMovesBackwardInHistoricalMode) {
    TimelineController ctrl;
    ctrl.on_new_sample(10'000'000'000ULL);
    ctrl.set_historical(5'000'000'000ULL);

    ctrl.scroll_by(-2'000'000'000LL); // -2 s
    EXPECT_EQ(ctrl.state().current_ts_ns, 3'000'000'000ULL);
}

TEST(TimelineController, ScrollByClampsToZero) {
    TimelineController ctrl;
    ctrl.on_new_sample(10'000'000'000ULL);
    ctrl.set_historical(1'000'000'000ULL);

    ctrl.scroll_by(-5'000'000'000LL); // would go negative
    EXPECT_EQ(ctrl.state().current_ts_ns, 0u);
}

TEST(TimelineController, ScrollByClampsToPresentInHistoricalMode) {
    TimelineController ctrl;
    ctrl.on_new_sample(5'000'000'000ULL);
    ctrl.set_historical(4'000'000'000ULL);

    ctrl.scroll_by(10'000'000'000LL); // would overshoot live head
    EXPECT_EQ(ctrl.state().current_ts_ns, 5'000'000'000ULL);
}

// ---------------------------------------------------------------------------
// on_new_sample
// ---------------------------------------------------------------------------

TEST(TimelineController, OnNewSampleUpdatesLatestTimestamp) {
    TimelineController ctrl;
    ctrl.on_new_sample(1'000'000'000ULL);
    ctrl.on_new_sample(3'000'000'000ULL);
    ctrl.on_new_sample(2'000'000'000ULL); // older — should not regress

    // Confirm cursor advanced in live mode
    EXPECT_EQ(ctrl.state().current_ts_ns, 3'000'000'000ULL);
}

TEST(TimelineController, OnNewSampleDoesNotMoveCursorInHistoricalMode) {
    TimelineController ctrl;
    ctrl.set_historical(1'000'000'000ULL);
    ctrl.on_new_sample(9'000'000'000ULL);

    // Cursor must stay at historical position
    EXPECT_EQ(ctrl.state().current_ts_ns, 1'000'000'000ULL);
    EXPECT_EQ(ctrl.state().mode, PlaybackMode::Historical);
}

TEST(TimelineController, PlaybackSpeedDefaultsToOne) {
    TimelineController ctrl;
    EXPECT_DOUBLE_EQ(ctrl.state().playback_speed, 1.0);
    ctrl.set_historical(0ULL);
    EXPECT_DOUBLE_EQ(ctrl.state().playback_speed, 1.0);
    ctrl.set_live();
    EXPECT_DOUBLE_EQ(ctrl.state().playback_speed, 1.0);
}
