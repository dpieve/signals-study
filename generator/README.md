# generator — ICU Real-Time Telemetry Simulator

A pure C11/C17 firmware/device simulator that generates realistic ICU biomedical
telemetry.  It runs a high-precision timing loop in a dedicated pthread, generates
multi-channel waveform data, and delivers it to the caller via a callback.

## What it does

- Spawns a single background thread that fires at a configurable rate (packets per second).
- Each tick generates one `GeneratorPacket` containing samples from all configured channels.
- Supports up to `GENERATOR_MAX_CHANNELS` (12) channels, each with its own simulation function.
- Built-in simulations: ECG (PQRST), SpO2, arterial blood pressure, EEG, white noise, flatline.
- Drift compensation: the loop tracks `next_tick_ns` and adjusts sleep duration each cycle to
  stay on schedule regardless of callback execution time.
- Optional additive Gaussian noise (Box-Muller), jitter (random sample drops), and packet-drop
  simulation for testing consumer resilience.
- On Linux, the timing thread attempts `SCHED_FIFO` real-time priority (silently ignored if
  unprivileged or on Windows/MSYS2).

## Directory layout

```
generator/
  generator.h                 Public API
  generator_internal.h        Internal state, clock-function-pointer, packet pool
  generator.c                 Core implementation
  generator_simulations.h     Simulation function declarations
  generator_simulations.c     ECG, SpO2, BP, EEG, noise, flatline implementations
  CMakeLists.txt
  tests/
    generator_tests.cpp       API and lifecycle tests
    timing_tests.cpp          Mock-clock drift-compensation tests
    simulation_tests.cpp      Waveform range and shape tests
```

## Building

Requires: CMake >= 3.20, a C11/C++23 compiler (GCC or Clang), pthreads, GTest.
On Windows use MSYS2 UCRT64 — `clock_gettime`, `nanosleep`, and pthreads are all available.

```bash
# From the repository root (or the generator directory):
cmake -B build -S generator -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Running the tests

```bash
cd build
ctest --output-on-failure
# or run directly:
./generator_tests
```

## Usage example

```c
#include "generator.h"
#include "generator_simulations.h"

static ChannelConfig channels[] = {
    { .channel_id = 0, .name = "ECG-II", .attachment = "chest",
      .simulation_fn = generator_sim_ecg, .simulation_user_data = NULL },
    { .channel_id = 1, .name = "SpO2",   .attachment = "finger",
      .simulation_fn = generator_sim_spo2, .simulation_user_data = NULL },
};

static void on_packet(const GeneratorPacket* pkt, void* ud) {
    (void)ud;
    printf("seq=%" PRIu64 " samples=%u\n", pkt->sequence, pkt->sample_count);
}

int main(void) {
    GeneratorConfig cfg = {
        .sample_rate            = 500,
        .packets_per_second     = 25,
        .channel_count          = 2,
        .channels               = channels,
        .max_samples_per_packet = 256,
        .noise_level            = 0.002,
        .jitter_probability     = 0.01,
        .packet_drop_probability = 0.0,
    };
    generator_start(&cfg, on_packet, NULL, NULL);
    // ... run for a while ...
    generator_stop();
    return 0;
}
```
