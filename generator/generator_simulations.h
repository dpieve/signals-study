#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * generator_simulations.h
 *
 * Biomedical waveform simulation functions for the ICU telemetry generator.
 * All functions conform to the GeneratorSimulationFn signature:
 *
 *   double fn(uint64_t timestamp_ns, uint32_t sample_index, void* user_data)
 *
 * They may be used directly as ChannelConfig.simulation_fn.
 */

/*
 * ECG — realistic PQRST waveform.
 *
 * Models each complex as a sum of Gaussian bumps at fixed fractions of the
 * RR interval:
 *   P  @ 15%  — small positive deflection
 *   Q  @ 35%  — small negative deflection
 *   R  @ 40%  — large positive peak (~1.0 mV)
 *   S  @ 45%  — negative deflection
 *   T  @ 65%  — broad positive wave
 *
 * Default HR: 72 bpm.
 * user_data: optional pointer to double — heart-rate override in bpm.
 */
double generator_sim_ecg(uint64_t timestamp_ns, uint32_t sample_index, void* user_data);

/*
 * SpO2 — pulse-oximetry pleth waveform.
 *
 * Sinusoidal signal in range [0.95, 1.00] at ~1 Hz (resting pulse rate ≈60 bpm).
 * A small AC component (~0.02) rides on the DC baseline.
 * user_data: ignored.
 */
double generator_sim_spo2(uint64_t timestamp_ns, uint32_t sample_index, void* user_data);

/*
 * Arterial blood-pressure waveform.
 *
 * Rapid systolic rise to ~120 mmHg, exponential diastolic decay to ~60 mmHg.
 * ~1 Hz pulse rate.
 * user_data: ignored.
 */
double generator_sim_bp(uint64_t timestamp_ns, uint32_t sample_index, void* user_data);

/*
 * EEG — bandlimited noise.
 *
 * Sum of sinusoids in theta (4–8 Hz), alpha (8–12 Hz), and beta (13–30 Hz)
 * bands with randomised phases.  Amplitude envelope ~50–100 µV (returned as
 * millivolts, i.e., 0.05–0.10 mV).
 * user_data: ignored.
 */
double generator_sim_eeg(uint64_t timestamp_ns, uint32_t sample_index, void* user_data);

/*
 * White noise — pure Gaussian noise with amplitude ~0.1.
 * user_data: ignored.
 */
double generator_sim_noise(uint64_t timestamp_ns, uint32_t sample_index, void* user_data);

/*
 * Flatline — always returns 0.0.
 * user_data: ignored.
 */
double generator_sim_flatline(uint64_t timestamp_ns, uint32_t sample_index, void* user_data);

#ifdef __cplusplus
} /* extern "C" */
#endif
