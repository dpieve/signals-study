# ICU Real-Time Telemetry Simulator

A full-stack biomedical telemetry simulation platform in C/C++23. Simulates ICU bedside monitor data acquisition, persists every sample to SQLite WAL, processes the committed data, streams it over gRPC, and renders it in a Raylib UI styled after real ICU monitors (Philips IntelliVue / GE CARESCAPE).

```
generator (C11) → ingestion queue → SQLite WAL → processing → gRPC → client UI (Raylib)
```

**Data is always persisted before processing or streaming.** SQLite is the authoritative source.

---

## Components

| Component | Language | Description |
|-----------|----------|-------------|
| `generator/` | C11 | Firmware simulator — multi-channel waveform generator with precise timing |
| `consumer/` | C++23 | Backend — ingestion, SQLite persistence, signal processing, gRPC streaming |
| `client/`   | C++23 | ICU bedside monitor UI using Raylib |

---

## Prerequisites

### Windows — MSYS2 UCRT64

Install [MSYS2](https://www.msys2.org/), then run once from any terminal:

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc          \
          mingw-w64-ucrt-x86_64-gdb           \
          mingw-w64-ucrt-x86_64-cmake         \
          mingw-w64-ucrt-x86_64-ninja         \
          mingw-w64-ucrt-x86_64-gtest         \
          mingw-w64-ucrt-x86_64-sqlite3       \
          mingw-w64-ucrt-x86_64-grpc          \
          mingw-w64-ucrt-x86_64-spdlog        \
          mingw-w64-ucrt-x86_64-raylib        \
          mingw-w64-ucrt-x86_64-concurrentqueue
```

The build uses **only MSYS2 system packages** — no downloads at configure time. Configure takes ~2 s.

Ensure `C:\msys64\ucrt64\bin` is in your Windows PATH (added by the MSYS2 installer).

### Linux / WSL (Ubuntu)

```bash
sudo apt update
sudo apt install -y \
    build-essential cmake ninja-build pkg-config \
    libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc \
    libsqlite3-dev libspdlog-dev libgtest-dev \
    libconcurrentqueue-dev
# Raylib must be built from source on Ubuntu < 24.04
# https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux
```

---

## Building

```bash
# 1. Configure (Debug) — ~2 s on Windows with MSYS2
cmake --preset debug

# 2. Build all targets
cmake --build --preset debug

# 3. Run all 120 tests
ctest --preset debug
```

### Available presets

| Preset | Type | Notes |
|--------|------|-------|
| `debug`   | Debug    | Default development build |
| `release` | Release  | LTO enabled |
| `tsan`    | Debug    | ThreadSanitizer — catches data races |
| `asan`    | Debug    | AddressSanitizer + UBSan |

```bash
cmake --preset release && cmake --build --preset release

cmake --preset tsan && cmake --build --preset tsan
ctest --preset tsan --output-on-failure
```

### Manual cmake (without presets)

```bash
cmake -S . -B build-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe \
  -DCMAKE_MAKE_PROGRAM=C:/msys64/ucrt64/bin/ninja.exe
cmake --build build-debug
```

---

## Running

Start the **consumer** first (it embeds the generator), then the **client**:

```bash
# Terminal 1 — backend (generator + persistence + gRPC server on :50051)
./build-debug/consumer/consumer.exe

# Terminal 2 — ICU monitor UI
./build-debug/client/icu_monitor.exe --server localhost:50051
```

Waveforms appear in the UI within 500 ms. Press **ESC** or close the window to exit the client; **Ctrl+C** to stop the consumer.

---

## Testing

```bash
# All 120 tests via CTest
ctest --preset debug --output-on-failure

# Individual binaries with verbose output
./build-debug/generator/generator_tests.exe --gtest_color=yes
./build-debug/consumer/consumer_tests.exe   --gtest_color=yes
./build-debug/client/client_tests.exe       --gtest_color=yes
```

### Test suites (120 tests total)

| Binary | Suite | What it covers |
|--------|-------|----------------|
| `generator_tests` | `SimulationECG/SpO2/BP/EEG/…` | Waveform outputs in physically plausible ranges |
| `generator_tests` | `TimingLoop` | Drift compensation via mock clock — no wall-clock assertions |
| `generator_tests` | `GeneratorConfig / GeneratorLifecycle` | Validation, start/stop, sequence monotonicity, packet sizing |
| `consumer_tests`  | `IngestionStress` | 4 producers × 10,000 packets, no duplicates |
| `consumer_tests`  | `PacketQueue` | Drop counter on full queue; producer never blocks |
| `consumer_tests`  | `SqliteWriter` | In-memory SQLite; WAL mode; flush_batch; schema_version |
| `consumer_tests`  | `SignalMetrics / ProcessingEngine` | RMS, min/max, moving average, noise — known-value assertions |
| `consumer_tests`  | `SubscribeValidation / HistoricalValidation / GrpcBroadcaster` | Channel validation, time-range checks |
| `consumer_tests`  | `ReplayTests` | ORDER BY timestamp, time-range filter |
| `client_tests`    | `ChannelRingBuffer` | Push/get_view wraparound; power-of-two invariant; oldest-first ordering |
| `client_tests`    | `TimelineController` | Live↔historical transitions; scroll clamping |
| `client_tests`    | `NavigationTests` | Timestamp jump accuracy; scroll bounds |
| `client_tests`    | `GrpcClientReconnect` | Exponential back-off arithmetic |

---

## VS Code

Required extensions: **C/C++ Extension Pack** and **CMake Tools**.

### First open

1. VS Code detects `CMakePresets.json` and asks to select a kit.
2. Pick **GCC 15 · MSYS2 UCRT64 (Windows)** (pre-defined in `.vscode/cmake-kits.json`).  
   On Linux, select the auto-detected GCC kit instead.
3. CMake Tools configures automatically. Done — no further setup.

Kit selection is remembered per workspace; you only do this once.

### F5 — build + debug

Select a configuration from the dropdown at the top of the Run & Debug panel, then press **F5**. CMake Tools builds the target (if changed) and launches GDB automatically.

| Configuration | What it debugs |
|---------------|----------------|
| **Consumer** | Telemetry backend |
| **ICU Monitor** | Raylib UI (connect consumer first) |
| **Generator Tests** | Generator unit test binary |
| **Consumer Tests** | Consumer unit test binary |
| **Client Tests** | Client unit test binary |

No paths, no tasks, no manual build step — CMake Tools handles everything.

### Other shortcuts

| Key / action | What it does |
|--------------|-------------|
| `F7` | CMake: Build (builds the active target) |
| `Ctrl+Shift+T` | Run All Tests (ctest via `tasks.json`) |
| CMake status bar ▶ | Run without debugger |
| CMake status bar 🐛 | Debug active target |

### Switching presets (debug / release / tsan / asan)

Use the **CMake Tools status bar** at the bottom of VS Code to switch the active configure preset. The kit stays the same; only the build flags change.

---

## Architecture

### Data flow

```
Generator (C11)
  timing loop: 1000 pkt/s · 44100 Hz · up to 12 channels
    └─ packet_callback()            [noexcept, memcpy only]
         └─ PacketQueue             [moodycamel, bounded lock-free]
              └─ PersistenceThread
                   └─ SqliteWriter::flush_batch()   [WAL, batched transactions]
                        └─ PersistenceEvents queue
                             └─ ProcessingEngine    [moving avg, RMS, min/max…]
                                  └─ GrpcBroadcaster::broadcast()   [proto Arena]
                                       └─ gRPC ServerWriter → Client
```

### Channels

Channels are **generic acquisition inputs** — the simulation function determines the waveform:

| # | Default attachment | Simulation |
|---|-------------------|------------|
| 0 | Left leg electrode | ECG (PQRST) |
| 1 | Finger sensor | SpO2 |
| 2 | Radial artery | Arterial BP |
| 3 | EEG temporal left | EEG (theta/alpha/beta) |

Edit the channel table in `consumer/app/app.cpp` to add or reassign channels (up to 12).

### Shutdown order

```
1. Stop generator thread    — wait for in-flight callback to return
2. Drain ingestion queue    — spin until empty
3. Flush persistence batch  — commit current open transaction
4. Drain processing queue
5. Drain gRPC broadcaster
6. Stop gRPC server
```

---

## Configuration

All telemetry parameters are set in code — nothing hardcoded:

```cpp
// consumer/config/consumer_config.h
ConsumerConfig {
    .sample_rate        = 44100,   // Hz
    .packets_per_second = 1000,
    .channel_count      = 4,
    .grpc_port          = 50051,
    .db_path            = "telemetry.db",
};
```

Change any value and rebuild. The packetisation math, ring-buffer sizing, and SQLite batch interval all adapt automatically.

---

## Dependencies

All sourced from MSYS2 UCRT64 packages.

| Library | Version | Purpose |
|---------|---------|---------|
| GCC | 15.2 | C11 + C++23 compiler |
| GDB | 17.1 | Debugger |
| [gRPC](https://grpc.io/) | 1.76 | Consumer ↔ Client streaming |
| [Protobuf](https://protobuf.dev/) | 6.33 | Wire serialisation |
| [SQLite3](https://sqlite.org/) | 3.52 | Telemetry persistence |
| [spdlog](https://github.com/gabime/spdlog) | 1.17 | Structured logging |
| [moodycamel ConcurrentQueue](https://github.com/cameron314/concurrentqueue) | 1.0.5 | Lock-free ingestion and event queues |
| [Raylib](https://www.raylib.com/) | 5.5 | ICU monitor UI |
| [GoogleTest](https://github.com/google/googletest) | 1.17 | Unit testing |

---

## Troubleshooting

**`0xc0000139` / missing DLL when running tests outside the preset**
```bash
PATH="C:/msys64/ucrt64/bin:$PATH" ctest --preset debug
```
`ctest --preset debug` injects this PATH automatically via `CMakePresets.json`.

**Port 50051 already in use**
Change `grpc_port` in `consumer/config/consumer_config.h` and pass `--server localhost:<port>` to the client.

**`std::flat_map` or `std::expected` not found**
Requires GCC ≥ 13 and `-std=c++23`. Verify: `g++ --version` (should print 13 or higher).
