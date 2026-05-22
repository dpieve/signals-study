/*
 * generator_simulations.c — Biomedical waveform simulation functions.
 *
 * All functions use only <math.h> and <stdlib.h>.
 * They produce physically plausible ICU-signal shapes for simulator use.
 */

#define _POSIX_C_SOURCE 200809L

#include "generator_simulations.h"
#include <math.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Utility: Gaussian bump
 *   Returns exp(-((x - centre)^2) / (2 * sigma^2)) * amplitude
 * ------------------------------------------------------------------------- */
static inline double gauss_bump(double x, double centre, double sigma, double amplitude)
{
    double d = (x - centre) / sigma;
    return amplitude * exp(-0.5 * d * d);
}

/* -------------------------------------------------------------------------
 * Utility: convert nanosecond timestamp to fractional seconds
 * ------------------------------------------------------------------------- */
static inline double ns_to_sec(uint64_t timestamp_ns)
{
    return (double)timestamp_ns * 1.0e-9;
}

/* =========================================================================
 * ECG — PQRST waveform
 * ========================================================================= */
double generator_sim_ecg(uint64_t timestamp_ns, uint32_t sample_index, void* user_data)
{
    (void)sample_index;

    /* Heart rate: default 72 bpm, overridable via user_data (double* bpm) */
    double hr_bpm = 72.0;
    if (user_data) {
        double override = *(const double*)user_data;
        if (override > 0.0)
            hr_bpm = override;
    }

    double rr_sec  = 60.0 / hr_bpm;          /* RR interval in seconds */
    double t       = ns_to_sec(timestamp_ns);
    double phase   = fmod(t, rr_sec) / rr_sec; /* 0..1 within one RR interval */

    /*
     * PQRST modelled as a sum of Gaussian bumps.
     * Parameters: (position, sigma, amplitude) — all in normalised phase [0,1].
     *
     *  P  @ 0.15  — small positive
     *  Q  @ 0.35  — small negative
     *  R  @ 0.40  — large positive
     *  S  @ 0.45  — moderate negative
     *  T  @ 0.65  — broad positive
     */
    double value = 0.0;
    value += gauss_bump(phase, 0.15, 0.020,  0.15);  /* P */
    value += gauss_bump(phase, 0.35, 0.012, -0.10);  /* Q */
    value += gauss_bump(phase, 0.40, 0.015,  1.00);  /* R */
    value += gauss_bump(phase, 0.45, 0.012, -0.25);  /* S */
    value += gauss_bump(phase, 0.65, 0.040,  0.30);  /* T */

    return value;
}

/* =========================================================================
 * SpO2 — pulse-oximetry plethysmography waveform
 * ========================================================================= */
double generator_sim_spo2(uint64_t timestamp_ns, uint32_t sample_index, void* user_data)
{
    (void)sample_index;
    (void)user_data;

    /*
     * Resting pulse ~60 bpm → 1 Hz fundamental.
     * DC baseline 0.975, AC modulation ±0.025.
     *
     * Add a small second-harmonic to simulate the dicrotic notch of the
     * real photoplethysmogram.
     */
    double t   = ns_to_sec(timestamp_ns);
    double f   = 1.0;  /* Hz */
    double dc  = 0.975;
    double ac  = 0.025;

    double wave = dc
                - ac * sin(2.0 * M_PI * f * t)         /* fundamental */
                + 0.005 * sin(4.0 * M_PI * f * t + 0.5); /* 2nd harmonic */

    return wave;
}

/* =========================================================================
 * Blood pressure — arterial waveform
 * ========================================================================= */
double generator_sim_bp(uint64_t timestamp_ns, uint32_t sample_index, void* user_data)
{
    (void)sample_index;
    (void)user_data;

    /*
     * Arterial pulse at ~1 Hz (60 bpm).
     * Each cycle:
     *   - Rapid systolic rise in the first 20% of the cycle (sine ramp)
     *   - Exponential diastolic decay over the remaining 80%
     * Pressure range: ~60 mmHg diastolic, ~120 mmHg systolic.
     */
    const double pulse_rate_hz = 1.0;
    const double systolic      = 120.0;
    const double diastolic     = 60.0;
    const double pulse_amp     = systolic - diastolic;  /* 60 mmHg */

    double t       = ns_to_sec(timestamp_ns);
    double period  = 1.0 / pulse_rate_hz;
    double phase   = fmod(t, period) / period;  /* 0..1 */

    double value;
    if (phase < 0.2) {
        /* Systolic upstroke: half-sine from 0 → 1 over first 20% */
        double up = sin(M_PI * phase / 0.2);
        value     = diastolic + pulse_amp * up;
    } else {
        /* Diastolic decay: exponential from peak back to diastolic */
        double decay_phase = (phase - 0.2) / 0.8;  /* 0..1 over remaining 80% */
        double decay       = exp(-4.0 * decay_phase);
        /* dicrotic notch: small bump at ~50% of decay */
        double notch       = 0.08 * pulse_amp
                           * gauss_bump(decay_phase, 0.25, 0.06, 1.0);
        value = diastolic + pulse_amp * decay + notch;
    }

    return value;
}

/* =========================================================================
 * EEG — bandlimited sinusoidal mixture
 * ========================================================================= */
double generator_sim_eeg(uint64_t timestamp_ns, uint32_t sample_index, void* user_data)
{
    (void)sample_index;
    (void)user_data;

    double t = ns_to_sec(timestamp_ns);

    /*
     * Fixed phase offsets give each component a distinct starting phase so
     * the signal is not purely symmetric.  These are compile-time constants
     * (no state needed — the function is pure w.r.t. timestamp).
     */

    /* Theta band: 4–8 Hz, amplitude ~75 µV = 0.075 mV */
    double theta = 0.0;
    theta += 0.030 * sin(2.0 * M_PI *  4.0 * t + 0.1);
    theta += 0.025 * sin(2.0 * M_PI *  6.0 * t + 1.2);
    theta += 0.020 * sin(2.0 * M_PI *  8.0 * t + 2.4);

    /* Alpha band: 8–12 Hz, amplitude ~90 µV = 0.090 mV */
    double alpha = 0.0;
    alpha += 0.040 * sin(2.0 * M_PI *  9.0 * t + 0.7);
    alpha += 0.035 * sin(2.0 * M_PI * 10.0 * t + 1.5);
    alpha += 0.015 * sin(2.0 * M_PI * 12.0 * t + 2.1);

    /* Beta band: 13–30 Hz, amplitude ~60 µV = 0.060 mV */
    double beta = 0.0;
    beta += 0.020 * sin(2.0 * M_PI * 15.0 * t + 0.3);
    beta += 0.015 * sin(2.0 * M_PI * 20.0 * t + 1.0);
    beta += 0.010 * sin(2.0 * M_PI * 25.0 * t + 1.8);
    beta += 0.008 * sin(2.0 * M_PI * 30.0 * t + 0.6);

    return theta + alpha + beta;
}

/* =========================================================================
 * White noise
 * ========================================================================= */
double generator_sim_noise(uint64_t timestamp_ns, uint32_t sample_index, void* user_data)
{
    (void)timestamp_ns;
    (void)sample_index;
    (void)user_data;

    /*
     * Return Gaussian white noise with standard deviation ~0.1.
     * Box-Muller via two uniform samples from rand().
     */
    double u1 = ((double)rand() / RAND_MAX);
    double u2 = ((double)rand() / RAND_MAX);
    /* clamp to avoid log(0) */
    if (u1 < 1e-10) u1 = 1e-10;
    double z = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
    return 0.1 * z;
}

/* =========================================================================
 * Flatline
 * ========================================================================= */
double generator_sim_flatline(uint64_t timestamp_ns, uint32_t sample_index, void* user_data)
{
    (void)timestamp_ns;
    (void)sample_index;
    (void)user_data;
    return 0.0;
}
