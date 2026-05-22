# ICU Telemetry Monitor — Client

A realistic ICU bedside monitor UI built with Raylib and C++23.
Connects to the `consumer` gRPC server and renders live/historical telemetry
in a style matching Philips IntelliVue / GE CARESCAPE monitors.

## What it does

- Receives processed telemetry via the `TelemetryService::SubscribeLive` gRPC stream
- Renders scrolling multi-channel waveforms in real time (60 FPS target)
- Displays numeric vitals panel (HR, SpO2, BP, RR) with large high-contrast values
- Top status bar: patient ID, date/time, profile name
- Bottom action bar: hardware-style rectangular buttons
- Automatic reconnect with exponential back-off (100 ms → 5 s) on connection loss
- Historical navigation: jump to any timestamp and scroll forward/backward

## Layout

```
+------------------------------------------------------+
| TOP STATUS BAR  patient ID | date/time | profile     |
+------------------------------------+-----------------+
|                                    |  HR    72       |
|   WAVEFORM AREA                    |  SpO2  98.6     |
|   scrolling waveforms (left→right) |  BP    120/80   |
|   stacked channels with grid       |  RR    16       |
+------------------------------------+-----------------+
| Main View | Menu | Alarm Limits | Trend | Profiles   |
+------------------------------------------------------+
```

## Building

### Prerequisites

- CMake >= 3.20
- C++23 compiler (GCC 13+, Clang 16+, or MSVC 2022+)
- [Raylib](https://www.raylib.com/) (installed via vcpkg or system package)
- gRPC + protobuf
- spdlog
- GoogleTest

### Build steps (from repository root)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target icu_monitor -j$(nproc)
```

### Running

```bash
# Start the consumer server first, then:
./build/client/icu_monitor --server localhost:50051 --channels 4
```

**Options:**

| Flag | Default | Description |
|---|---|---|
| `--server <host:port>` | `localhost:50051` | gRPC server address |
| `--channels <n>` | `4` | Number of channels (1–12) |
| `--width <px>` | `1280` | Window width |
| `--height <px>` | `800` | Window height |
| `--fps <n>` | `60` | Target frame rate |

### Running tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target client_tests -j$(nproc)
./build/client/client_tests
```

### ThreadSanitizer build

```bash
cmake -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DENABLE_TSAN=ON
cmake --build build-tsan --target icu_monitor -j$(nproc)
```

## Architecture

```
gRPC receive thread
        |  on_packet() [lock-free ring buffer push + mutex for latest_values_]
        v
ChannelRingBuffer (per channel, atomic write_pos_)
        |  get_view() [acquire load, ordered span]
        v
WaveformRenderer::draw_waveform()  →  Raylib DrawLineStrip / DrawLineEx
```

Key files:

| File | Purpose |
|---|---|
| `app/client_app.cpp` | Window lifecycle, render loop, gRPC startup |
| `ui/telemetry_view.cpp` | Frame composition, packet routing |
| `rendering/render_buffers.cpp` | Lock-free circular waveform buffer |
| `rendering/waveform_renderer.cpp` | Anti-aliased waveform drawing |
| `networking/grpc_client.cpp` | Connection, streaming, backoff |
| `timeline/timeline_controller.cpp` | Live / historical cursor |
