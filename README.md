# ICU Real-Time Telemetry Simulator

A full-stack biomedical telemetry simulation platform built in C/C++23. Simulates ICU bedside monitor data acquisition, persists it to SQLite, processes it, streams it via gRPC, and renders it in a Raylib UI that mimics real ICU monitors (Philips IntelliVue / GE CARESCAPE style).

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

### Windows (MSYS2 UCRT64) — Recommended

Install [MSYS2](https://www.msys2.org/), then open the **UCRT64** shell and run:

```bash
# Core toolchain (likely already installed)
pacman -S mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-ninja \
          mingw-w64-ucrt-x86_64-make

# SQLite (already installed in MSYS2)
pacman -S mingw-w64-ucrt-x86_64-sqlite3

# gRPC + Protobuf (STRONGLY recommended — avoids a 15-min FetchContent build)
pacman -S mingw-w64-ucrt-x86_64-grpc

# spdlog
pacman -S mingw-w64-ucrt-x86_64-spdlog

# Raylib
pacman -S mingw-w64-ucrt-x86_64-raylib

# GDB (for debugging)
pacman -S mingw-w64-ucrt-x86_64-gdb
```

> **Note on gRPC:** If you skip `pacman -S ... grpc`, CMake will automatically fetch gRPC from GitHub via FetchContent. This works but takes ~10–15 minutes on first build. Installing via pacman is much faster.

### Linux / WSL (Ubuntu)

```bash
sudo apt update
sudo apt install -y \
    build-essential cmake ninja-build \
    libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc \
    libsqlite3-dev \
    libspdlog-dev \
    libraylib-dev \
    libgtest-dev
```

---

## Building

### Quick start (MSYS2 UCRT64 terminal or VS Code tasks)

> **IMPORTANT — First-time build:** CMake uses FetchContent to automatically
> download gRPC, Raylib, spdlog, GoogleTest, and moodycamel if they are not
> already installed. The gRPC download alone is ~350 MB and takes **10–15 min
> to configure** + **20–40 min to compile** on first run.
> 
> **Strongly recommended:** Install the MSYS2 packages first (see Prerequisites
> above). The configure step drops from ~15 min to <30 s, and compilation from
> ~40 min to ~2 min.

```bash
# 1. Enter the project
cd /e/00-dev/projects/signals-study   # MSYS2 UCRT64 shell path
# or PowerShell: cd E:/00-dev/projects/signals-study

# 2. Configure (Debug)
cmake --preset debug

# 3. Build everything (add -j8 for parallel jobs)
cmake --build --preset debug

# 4. Run all tests
ctest --preset debug
```

### Available presets

| Preset | Build type | Notes |
|--------|-----------|-------|
| `debug`   | Debug    | Default development build |
| `release` | Release  | LTO enabled |
| `tsan`    | Debug + TSan | ThreadSanitizer — use to catch data races |
| `asan`    | Debug + ASan | AddressSanitizer + UBSan |

```bash
cmake --preset release && cmake --build --preset release

cmake --preset tsan   && cmake --build --preset tsan
ctest --preset tsan
```

### Manual cmake (if presets don't work)

```bash
mkdir build-debug && cd build-debug
cmake .. \
  -G "MinGW Makefiles" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe
cmake --build . --parallel 8
```

---

## Running

Start the **consumer** (backend) first, then the **client** (UI):

### Terminal 1 — Consumer

```bash
./build-debug/consumer/consumer.exe
# Linux: ./build-debug/consumer/consumer
```

The consumer starts the generator internally, persists telemetry to `telemetry.db`, and listens on gRPC port **50051**.

### Terminal 2 — Client

```bash
./build-debug/client/icu_monitor.exe --server localhost:50051
# Linux: ./build-debug/client/icu_monitor --server localhost:50051
```

The ICU monitor UI window opens. Waveforms should start rendering within 500 ms.

---

## Testing

```bash
# All tests
ctest --preset debug --output-on-failure

# Individual test binaries (with verbose GTest output)
./build-debug/generator/generator_tests.exe --gtest_color=yes
./build-debug/consumer/consumer_tests.exe   --gtest_color=yes
./build-debug/client/client_tests.exe       --gtest_color=yes

# Run under ThreadSanitizer (catches data races)
cmake --preset tsan && cmake --build --preset tsan
ctest --preset tsan --output-on-failure
```

### Test suites

| Suite | File | What it tests |
|-------|------|---------------|
| Generator timing | `generator/tests/timing_tests.cpp` | Drift compensation with mock clock — no wall-clock assertions |
| Generator waveforms | `generator/tests/generator_tests.cpp` | Callback correctness, sequence monotonicity, packet sizing |
| Simulation functions | `generator/tests/simulation_tests.cpp` | ECG/SpO2/BP/EEG output in physically plausible ranges |
| Ingestion stress | `consumer/tests/ingestion_tests.cpp` | 4 producers × 10,000 packets, assert no duplicates |
| Queue bounds | `consumer/tests/queue_tests.cpp` | Drop counter on full queue, no blocking |
| Persistence | `consumer/tests/persistence_tests.cpp` | In-memory SQLite, WAL mode, flush_batch |
| Signal metrics | `consumer/tests/processing_tests.cpp` | Pure functional: RMS, min/max, moving average |
| gRPC service | `consumer/tests/grpc_tests.cpp` | In-process gRPC channel, validation |
| Historical replay | `consumer/tests/replay_tests.cpp` | Ordering, post-commit events |
| Render buffers | `client/tests/render_buffers_tests.cpp` | Ring buffer wraparound (single-threaded, no Raylib) |
| Timeline | `client/tests/timeline_tests.cpp` | Cursor math, live↔historical transition |
| Navigation | `client/tests/navigation_tests.cpp` | Historical scroll bounds |

---

## VS Code

Open the project folder in VS Code. Install the **C/C++ Extension Pack** and **CMake Tools**.

### Tasks (`Ctrl+Shift+B`)

| Task | Description |
|------|-------------|
| **Build: All (Debug)** *(default)* | Configure + build everything in Debug mode |
| Build: All (Release) | Configure + build in Release mode |
| Build: Consumer only (Debug) | Fast rebuild of consumer only |
| Build: Client only (Debug) | Fast rebuild of client only |
| Build: All (TSan) | ThreadSanitizer build |
| **Test: All** *(default test)* | Run all CTest tests |
| Test: Generator | Generator tests only |
| Test: Consumer | Consumer tests only |
| Test: Client | Client tests only |
| Run: Consumer | Start the consumer backend |
| Run: Client (ICU Monitor UI) | Start the Raylib UI |

### Debug (`F5`)

| Configuration | What it does |
|---------------|-------------|
| Debug: Consumer | Attach GDB to consumer process |
| Debug: Client (ICU Monitor) | Attach GDB to Raylib UI |
| Debug: Generator Tests | Step through generator unit tests |
| Debug: Consumer Tests | Step through consumer unit tests |
| Debug: Client Tests | Step through client unit tests |

> **GDB path:** Configured for `C:/msys64/ucrt64/bin/gdb.exe`. If yours is elsewhere, update `.vscode/launch.json`.

---

## Architecture

### Data flow

```
Generator (C11)
  └─ timing loop @ 1000 pkt/s, 44100 Hz
       └─ packet_callback()
            └─ IngestionManager::packet_callback()  [noexcept, no alloc]
                 └─ PacketQueue (moodycamel, bounded)
                      └─ PersistenceThread
                           └─ SqliteWriter::flush_batch()  [WAL, batched]
                                └─ PersistenceEvents queue
                                     └─ ProcessingEngine::process_batch()
                                          └─ GrpcBroadcaster::broadcast()
                                               └─ gRPC ServerWriter → Client
```

### Channels

Channels are **generic acquisition inputs**. The simulation function assigned to each channel determines the waveform:

| Channel | Example attachment | Simulation |
|---------|-------------------|------------|
| 0 | Left leg electrode | ECG |
| 1 | Right arm electrode | ECG |
| 2 | Finger SpO2 sensor | SpO2 |
| 3 | EEG temporal left | EEG |

Add more channels by editing the channel config in `consumer/app/app.cpp`.

### Shutdown ordering

```
1. Stop generator   (wait for in-flight callback)
2. Drain ingestion queue
3. Flush persistence batch (commit current transaction)
4. Drain processing queue
5. Drain gRPC broadcaster queue
```

---

## Configuration

All telemetry characteristics are configurable in code (no hardcoded values):

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

Change any value and rebuild — the pipeline adapts automatically.

---

## Dependencies

| Library | Purpose | Source |
|---------|---------|--------|
| [gRPC](https://grpc.io/) | Consumer↔Client streaming | pacman or FetchContent |
| [Protobuf](https://protobuf.dev/) | Serialization | with gRPC |
| [SQLite3](https://sqlite.org/) | Telemetry persistence | pacman or FetchContent |
| [spdlog](https://github.com/gabime/spdlog) | Logging | pacman or FetchContent |
| [moodycamel ConcurrentQueue](https://github.com/cameron314/concurrentqueue) | Lock-free queues | FetchContent |
| [Raylib](https://www.raylib.com/) | ICU monitor UI | pacman or FetchContent |
| [GoogleTest](https://github.com/google/googletest) | Unit testing | FetchContent |

---

## Troubleshooting

**gRPC build takes forever**
```bash
pacman -S mingw-w64-ucrt-x86_64-grpc
```
Then re-run cmake configure. It will find the system package instead.

**`std::flat_map` not found**
Requires GCC 13+ and `-std=c++23`. Verify: `g++ --version` should be ≥ 13.

**`std::expected` not found**
Same requirement. GCC 12+ with `-std=c++23`.

**Port 50051 in use**
```bash
# Change grpc_port in consumer/config/consumer_config.h
```

**GDB not found for VS Code debugging**
```bash
pacman -S mingw-w64-ucrt-x86_64-gdb
```
Update `miDebuggerPath` in `.vscode/launch.json` if installed elsewhere.
