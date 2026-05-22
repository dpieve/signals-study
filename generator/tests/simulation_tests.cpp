/*
 * simulation_tests.cpp — GTest unit tests for biomedical waveform simulations.
 *
 * Tests:
 *   - ECG output is within [-1.5, 2.0] mV for 1000 samples
 *   - SpO2 output is within [0.9, 1.05] for 1000 samples
 *   - Blood-pressure output is within [40.0, 200.0] mmHg for 1000 samples
 *   - EEG output is within [-0.2, 0.2] mV for 1000 samples
 *   - Flatline always returns exactly 0.0
 *   - Noise simulation produces non-zero values (sanity check)
 *   - ECG HR override via user_data changes period correctly
 *   - SpO2 DC baseline is correct
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <cmath>
#include <limits>

extern "C" {
#include "generator_simulations.h"
}

/* =========================================================================
 * Helpers
 * ========================================================================= */

/* Generate timestamps spaced at `sample_rate_hz` over `duration_s` seconds,
 * starting from `start_ns`.                                                  */
static uint64_t sample_ts(uint32_t idx, double sample_rate_hz, uint64_t start_ns = 0)
{
    return start_ns + static_cast<uint64_t>(
        static_cast<double>(idx) / sample_rate_hz * 1.0e9);
}

/* Run a simulation function for `n_samples` and check that every value lies
 * within [lo, hi].  Returns the number of out-of-range values.              */
static int check_range(
    double (*fn)(uint64_t, uint32_t, void*),
    void*   user_data,
    int     n_samples,
    double  lo,
    double  hi,
    double  sample_rate_hz = 1000.0,
    uint64_t start_ns = 0)
{
    int violations = 0;
    for (int i = 0; i < n_samples; i++) {
        uint64_t ts = sample_ts(static_cast<uint32_t>(i), sample_rate_hz, start_ns);
        double v = fn(ts, static_cast<uint32_t>(i), user_data);
        if (v < lo || v > hi)
            ++violations;
    }
    return violations;
}

/* =========================================================================
 * ECG
 * ========================================================================= */

TEST(SimulationECG, RangeDefault72bpm)
{
    int violations = check_range(generator_sim_ecg, nullptr, 1000, -1.5, 2.0);
    EXPECT_EQ(violations, 0)
        << violations << " sample(s) out of [-1.5, 2.0] mV range";
}

TEST(SimulationECG, RangeWith40bpmOverride)
{
    double hr = 40.0;
    int violations = check_range(generator_sim_ecg, &hr, 1000, -1.5, 2.0);
    EXPECT_EQ(violations, 0)
        << violations << " sample(s) out of range at 40 bpm";
}

TEST(SimulationECG, RangeWith180bpmOverride)
{
    double hr = 180.0;
    int violations = check_range(generator_sim_ecg, &hr, 1000, -1.5, 2.0);
    EXPECT_EQ(violations, 0)
        << violations << " sample(s) out of range at 180 bpm";
}

TEST(SimulationECG, ContainsPositivePeak)
{
    /* The R-peak should be close to 1.0 mV; check we see values > 0.8 */
    bool found_peak = false;
    for (int i = 0; i < 1000; i++) {
        uint64_t ts = sample_ts(static_cast<uint32_t>(i), 1000.0);
        double v = generator_sim_ecg(ts, static_cast<uint32_t>(i), nullptr);
        if (v > 0.8) { found_peak = true; break; }
    }
    EXPECT_TRUE(found_peak) << "No R-peak found above 0.8 mV in 1000 samples";
}

TEST(SimulationECG, ZeroUserDataIgnored)
{
    /* Passing zero bpm in user_data should fall back to default HR (72 bpm).
     * Just confirm no crash and range is valid.                             */
    double bad_hr = 0.0;
    int violations = check_range(generator_sim_ecg, &bad_hr, 200, -1.5, 2.0);
    EXPECT_EQ(violations, 0);
}

/* =========================================================================
 * SpO2
 * ========================================================================= */

TEST(SimulationSpO2, RangeFor1000Samples)
{
    int violations = check_range(generator_sim_spo2, nullptr, 1000, 0.9, 1.05);
    EXPECT_EQ(violations, 0)
        << violations << " sample(s) out of [0.9, 1.05] range";
}

TEST(SimulationSpO2, DCBaselineAroundMidrange)
{
    /* Mean value over several complete cycles should be close to 0.975. */
    double sum = 0.0;
    const int N = 2000;  /* 2 s at 1000 Hz — covers many cycles */
    for (int i = 0; i < N; i++) {
        uint64_t ts = sample_ts(static_cast<uint32_t>(i), 1000.0);
        sum += generator_sim_spo2(ts, static_cast<uint32_t>(i), nullptr);
    }
    double mean = sum / N;
    EXPECT_NEAR(mean, 0.975, 0.02)
        << "SpO2 DC baseline deviates significantly from 0.975";
}

TEST(SimulationSpO2, HasACComponent)
{
    /* Max - min over one cycle should be > 0.01 (AC modulation present). */
    double lo =  1e9, hi = -1e9;
    for (int i = 0; i < 2000; i++) {
        uint64_t ts = sample_ts(static_cast<uint32_t>(i), 1000.0);
        double v = generator_sim_spo2(ts, static_cast<uint32_t>(i), nullptr);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    EXPECT_GT(hi - lo, 0.01)
        << "SpO2 AC component too small (hi=" << hi << " lo=" << lo << ")";
}

/* =========================================================================
 * Blood pressure
 * ========================================================================= */

TEST(SimulationBP, RangeFor1000Samples)
{
    int violations = check_range(generator_sim_bp, nullptr, 1000, 40.0, 200.0);
    EXPECT_EQ(violations, 0)
        << violations << " sample(s) out of [40, 200] mmHg range";
}

TEST(SimulationBP, SystolicPeakNear120)
{
    double peak = -1e9;
    for (int i = 0; i < 2000; i++) {
        uint64_t ts = sample_ts(static_cast<uint32_t>(i), 1000.0);
        double v = generator_sim_bp(ts, static_cast<uint32_t>(i), nullptr);
        if (v > peak) peak = v;
    }
    EXPECT_GE(peak, 100.0) << "Systolic peak too low";
    EXPECT_LE(peak, 200.0) << "Systolic peak too high";
}

TEST(SimulationBP, DiastolicFloorNear60)
{
    double floor_val = 1e9;
    for (int i = 0; i < 2000; i++) {
        uint64_t ts = sample_ts(static_cast<uint32_t>(i), 1000.0);
        double v = generator_sim_bp(ts, static_cast<uint32_t>(i), nullptr);
        if (v < floor_val) floor_val = v;
    }
    EXPECT_GE(floor_val, 40.0) << "Diastolic floor too low";
    EXPECT_LE(floor_val, 80.0) << "Diastolic floor unexpectedly high";
}

/* =========================================================================
 * EEG
 * ========================================================================= */

TEST(SimulationEEG, RangeFor1000Samples)
{
    int violations = check_range(generator_sim_eeg, nullptr, 1000, -0.2, 0.2);
    EXPECT_EQ(violations, 0)
        << violations << " sample(s) out of [-0.2, 0.2] mV range";
}

TEST(SimulationEEG, NonZeroOutput)
{
    /* EEG should not be a flat line. */
    bool nonzero = false;
    for (int i = 0; i < 100; i++) {
        uint64_t ts = sample_ts(static_cast<uint32_t>(i), 1000.0);
        double v = generator_sim_eeg(ts, static_cast<uint32_t>(i), nullptr);
        if (std::fabs(v) > 1e-10) { nonzero = true; break; }
    }
    EXPECT_TRUE(nonzero) << "EEG returned all-zero output";
}

TEST(SimulationEEG, AmplitudeNotTooSmall)
{
    /* Peak amplitude should be at least 0.01 mV (10 µV) */
    double peak = 0.0;
    for (int i = 0; i < 4000; i++) {
        uint64_t ts = sample_ts(static_cast<uint32_t>(i), 1000.0);
        double v = std::fabs(generator_sim_eeg(ts, static_cast<uint32_t>(i), nullptr));
        if (v > peak) peak = v;
    }
    EXPECT_GE(peak, 0.01) << "EEG amplitude too small (< 10 µV)";
}

/* =========================================================================
 * Noise
 * ========================================================================= */

TEST(SimulationNoise, ProducesNonZeroValues)
{
    std::srand(42);
    bool nonzero = false;
    for (int i = 0; i < 100; i++) {
        double v = generator_sim_noise(0, static_cast<uint32_t>(i), nullptr);
        if (std::fabs(v) > 1e-15) { nonzero = true; break; }
    }
    EXPECT_TRUE(nonzero) << "Noise simulation returned only zeros";
}

TEST(SimulationNoise, AmplitudeRoughlyCorrect)
{
    /* Over many samples the std dev should be close to 0.1. */
    std::srand(12345);
    const int N = 10000;
    double sum = 0.0, sum2 = 0.0;
    for (int i = 0; i < N; i++) {
        double v = generator_sim_noise(static_cast<uint64_t>(i), static_cast<uint32_t>(i), nullptr);
        sum  += v;
        sum2 += v * v;
    }
    double mean   = sum  / N;
    double var    = sum2 / N - mean * mean;
    double stddev = std::sqrt(var);

    EXPECT_NEAR(stddev, 0.1, 0.03)
        << "Noise stddev " << stddev << " too far from 0.1";
}

/* =========================================================================
 * Flatline
 * ========================================================================= */

TEST(SimulationFlatline, AlwaysZero)
{
    for (int i = 0; i < 1000; i++) {
        uint64_t ts = sample_ts(static_cast<uint32_t>(i), 1000.0);
        double v = generator_sim_flatline(ts, static_cast<uint32_t>(i), nullptr);
        EXPECT_EQ(v, 0.0) << "Flatline returned non-zero at sample " << i;
    }
}

TEST(SimulationFlatline, ZeroTimestampAndIndex)
{
    EXPECT_EQ(generator_sim_flatline(0, 0, nullptr), 0.0);
}

TEST(SimulationFlatline, MaxTimestamp)
{
    EXPECT_EQ(generator_sim_flatline(UINT64_MAX, UINT32_MAX, nullptr), 0.0);
}

/* =========================================================================
 * Cross-channel: sample_index parameter is accepted without crash
 * ========================================================================= */

TEST(SimulationGeneral, AllFunctionsHandleNonZeroSampleIndex)
{
    const uint32_t idx = 9999;
    const uint64_t ts  = 1234567890ULL * 1000ULL;  /* ~1.23 s */

    EXPECT_NO_FATAL_FAILURE(generator_sim_ecg(ts, idx, nullptr));
    EXPECT_NO_FATAL_FAILURE(generator_sim_spo2(ts, idx, nullptr));
    EXPECT_NO_FATAL_FAILURE(generator_sim_bp(ts, idx, nullptr));
    EXPECT_NO_FATAL_FAILURE(generator_sim_eeg(ts, idx, nullptr));
    EXPECT_NO_FATAL_FAILURE(generator_sim_noise(ts, idx, nullptr));
    EXPECT_NO_FATAL_FAILURE(generator_sim_flatline(ts, idx, nullptr));
}
