ICU Real-Time Telemetry Simulator — Final Reviewed Implementation Plan
Objective

Build a realistic ICU telemetry simulation platform composed of:

generator — firmware/device simulator in pure C
consumer — telemetry backend in C++23
client — realistic ICU bedside monitor UI in C++23 using Raylib

The system must:

simulate biomedical telemetry acquisition
continuously persist telemetry
process persisted telemetry
stream processed telemetry to connected clients
support historical replay/navigation
mimic real ICU bedside monitors visually and behaviorally
maintain low-latency visualization
minimize packet loss

Target environments:

Linux
Windows WSL (Ubuntu)
Core Requirements
Telemetry Characteristics

All telemetry characteristics must be configurable in code.

Nothing should be hardcoded.

Requirement	Default
Sample Rate	44100 Hz
Packet Rate	1000 packets/sec
Channels	Up to 12
UI Latency Target	< 500 ms
Persistence	Always enabled
Downsampling	Forbidden
Historical Navigation	Required
Packet Loss	Minimize aggressively

The architecture must automatically adapt if:

sample rate changes
packet rate changes
channel count changes
simulation functions change

without redesigning:

persistence
processing
streaming
rendering
Core Architectural Concept

Channels are generic acquisition inputs.

A channel does NOT intrinsically represent:

ECG
EEG
SPO2
Blood pressure

Instead:

a channel represents a physical acquisition input
a sensor attachment
a patient connection point

Examples:

channel 1 -> left leg electrode
channel 2 -> right arm electrode
channel 3 -> SPO2 finger sensor
channel 4 -> EEG temporal electrode

The simulation function assigned to the channel determines the waveform generated.

The generator itself remains generic.

Simulation Model

The generator exposes reusable simulation functions.

Examples:

ECG waveform
SPO2 waveform
Blood pressure waveform
EEG waveform
Noise
Flatline

Each channel is configured with:

metadata
attachment description
simulation callback/function

Example:

Channel 1 -> "Left Leg Electrode" -> ECG simulation
Channel 2 -> "Right Arm Electrode" -> ECG simulation
Channel 3 -> "Finger SPO2 Sensor" -> SPO2 simulation
Channel 4 -> "EEG Temporal Left" -> EEG simulation
Required System Architecture
Generator
    ->
Ingestion Queue
    ->
SQLite Persistence
    ->
Post-Commit Event
    ->
Processing Engine
    ->
gRPC Broadcast
    ->
Client UI

Critical rule:

Data MUST be persisted before processing and before UI streaming.

The database is the authoritative telemetry source.

The UI only receives:

persisted telemetry
processed telemetry
ordered telemetry
Architectural Principles

The system must prioritize:

Persistence-first design
Deterministic ordering
Non-blocking ingestion
Minimal packet loss
Replay fidelity
Bounded memory usage
Low-latency visualization
Configurability
Feature-oriented architecture
Realistic ICU monitor behavior
Future scalability
Project Organization Rules

Use a vertical-slice / feature-oriented structure.

Do NOT separate files into:

include/
src/

Files belonging to the same feature/module must remain together.

Example:

generator.c
generator.h

must remain side-by-side.

The repository should prioritize:

discoverability
modularity
easy navigation
feature locality
1. generator

Language:

C11/C17

Requirements:

Pure C API
No C++
Timing-sensitive
Configurable
Lightweight
Generator Structure
generator/
├── generator.c
├── generator.h
├── generator_simulations.c
├── generator_simulations.h
├── generator_internal.h
├── tests/
│   ├── generator_tests.cpp
│   ├── timing_tests.cpp
│   └── simulation_tests.cpp
├── CMakeLists.txt
└── README.md
Generator Responsibilities
simulate telemetry acquisition
manage channels
execute simulation functions
generate packets
maintain precise timing
emit lifecycle events
Generator API

/* Maximum number of channels. Used for static internal allocation.
   GeneratorConfig passes channels as a pointer+count — not a fixed array. */
#define GENERATOR_MAX_CHANNELS 12

typedef enum GeneratorError {
    GENERATOR_OK = 0,
    GENERATOR_ERROR_INVALID_CONFIG,
    GENERATOR_ERROR_ALREADY_RUNNING,
    GENERATOR_ERROR_THREAD_FAILED
} GeneratorError;

typedef enum GeneratorEventType {
    GENERATOR_EVENT_STARTED,
    GENERATOR_EVENT_STOPPED,
    GENERATOR_EVENT_FAILED
} GeneratorEventType;

/* double, not float: SQLite REAL is IEEE 754 double.
   Using float would silently lose precision before persistence.
   ECG/EEG signals require sub-millivolt resolution. */
typedef struct GeneratorSample {
    uint64_t timestamp_ns;
    uint32_t channel_id;
    double   value;
} GeneratorSample;

/* Ownership contract: the `samples` pointer is valid ONLY within the
   callback body. The consumer MUST memcpy before returning.
   Allocate as a single flat block (struct + trailing array) so the
   packet and its samples are freed atomically.
   `samples_capacity` is the allocated capacity; `sample_count <= samples_capacity`. */
typedef struct GeneratorPacket {
    uint64_t  sequence;
    uint32_t  sample_count;
    uint32_t  samples_capacity;
    GeneratorSample* samples;
} GeneratorPacket;

/* `void* simulation_user_data` is idiomatic C type-erasure.
   On the C++ consumer side, wrap this in a std::function<double(uint64_t, uint32_t)>
   lambda capture to eliminate the void* entirely. */
typedef double (*GeneratorSimulationFn)(
    uint64_t timestamp_ns,
    uint32_t sample_index,
    void* simulation_user_data);

/* Fixed-size char arrays make ChannelConfig self-contained and safe to copy.
   No dangling-pointer risk from caller-owned string literals. */
typedef struct ChannelConfig {
    uint32_t channel_id;

    char name[64];
    char attachment[64];

    GeneratorSimulationFn simulation_fn;
    void* simulation_user_data;
} ChannelConfig;

/* `channels` is a pointer+count, not a fixed array — nothing hardcoded.
   The caller owns the array; it must remain valid for the lifetime of the generator. */
typedef struct GeneratorConfig {
    uint32_t sample_rate;
    uint32_t packets_per_second;

    uint32_t            channel_count;
    const ChannelConfig* channels;

    uint32_t max_samples_per_packet;

    double noise_level;
    double jitter_probability;
    double packet_drop_probability;
} GeneratorConfig;

typedef void (*GeneratorPacketCallback)(
    const GeneratorPacket* packet,
    void* user_data);

/* `error_code` is set when event == GENERATOR_EVENT_FAILED.
   Callers must not parse `message` to distinguish error types. */
typedef void (*GeneratorEventCallback)(
    GeneratorEventType event,
    int                error_code,
    const char*        message,
    void*              user_data);

/* Validates config before starting. Returns GENERATOR_ERROR_INVALID_CONFIG
   if any field is out of range or a simulation_fn pointer is NULL.
   Called automatically by generator_start. */
GeneratorError generator_config_validate(const GeneratorConfig* config);

GeneratorError generator_start(
    const GeneratorConfig*  config,
    GeneratorPacketCallback packet_callback,
    GeneratorEventCallback  event_callback,
    void*                   user_data);

GeneratorError generator_stop(void);
Built-In Simulation Functions

Examples:

double generator_sim_ecg(...);
double generator_sim_spo2(...);
double generator_sim_bp(...);
double generator_sim_eeg(...);
double generator_sim_noise(...);
double generator_sim_flatline(...);

The application configures which function each channel uses.

Generator Timing Requirements

Generator must:

use monotonic clocks
compensate timing drift
maintain sequence continuity

Use:

clock_gettime(CLOCK_MONOTONIC)
nanosleep()
Packetization

Packet sizing must adapt dynamically from configuration.

The generator computes:

samples per packet
timing intervals
drift compensation

based on:

sample rate
packets/sec

Avoid hardcoded packet sizing.

2. consumer

Language:

C++23

Responsibilities:

ingest telemetry
persist telemetry
emit post-commit events
process telemetry
stream processed telemetry
support historical replay
Consumer Structure
consumer/
├── app/
│   ├── main.cpp
│   ├── app.cpp
│   └── app.h
│
├── ingestion/
│   ├── ingestion_manager.cpp
│   ├── ingestion_manager.h
│   ├── packet_queue.cpp
│   └── packet_queue.h
│
├── persistence/
│   ├── sqlite_writer.cpp
│   ├── sqlite_writer.h
│   ├── persistence_events.cpp
│   └── persistence_events.h
│
├── processing/
│   ├── processing_engine.cpp
│   ├── processing_engine.h
│   ├── signal_metrics.cpp
│   └── signal_metrics.h
│
├── grpc/
│   ├── telemetry_service.cpp
│   ├── telemetry_service.h
│   ├── telemetry.proto
│   └── grpc_broadcaster.cpp
│
├── config/
│   ├── consumer_config.h
│   └── runtime_config.h
│
├── common/
│   ├── telemetry_types.h
│   ├── timestamps.h
│   ├── logging.h
│   └── thread_utils.h
│
├── tests/
│   ├── ingestion_tests.cpp
│   ├── persistence_tests.cpp
│   ├── processing_tests.cpp
│   ├── grpc_tests.cpp
│   └── replay_tests.cpp
│
├── CMakeLists.txt
└── README.md
Consumer Pipeline
Generator Callback
        |
        v
Lock-Free Queue
        |
        v
Persistence Thread
        |
        | COMMIT SUCCESS
        v
Persisted Event Queue
        |
        v
Processing Engine
        |
        v
Processed Event Queue
        |
        v
gRPC Broadcast
Critical Rule

Generator callback MUST NEVER BLOCK.

Inside callback:

copy/enqueue packet
return immediately

Forbidden:

DB access
gRPC calls
filesystem operations
heavy allocations
C++ Type Definitions

Use a strong ChannelId type throughout consumer and client to prevent accidental arithmetic:

// consumer/common/telemetry_types.h
enum class ChannelId : uint32_t {};

Queue elements must be trivially copyable with no internal pointers — flat fixed-size arrays, statically bounded by MAX_SAMPLES_PER_PACKET:

struct IngestedPacket {
    uint64_t sequence;
    uint64_t first_timestamp_ns;
    uint32_t sample_count;
    std::array<GeneratorSample, MAX_SAMPLES_PER_PACKET> samples;
};
static_assert(std::is_trivially_copyable_v<IngestedPacket>);

The post-commit event queue carries in-memory batches, moved (not copied) from the persistence thread:

struct CommittedBatch {
    std::vector<IngestedPacket> packets;
    uint64_t commit_sequence;
};

Post-commit event mechanism: moodycamel::ConcurrentQueue<CommittedBatch>.

Use std::span<const T> for all buffer view parameters in C++ APIs — replace every const T* ptr, size_t count pair.

Processing results use std::variant to avoid virtual dispatch on the hot path:

using ProcessingResult = std::variant<
    MovingAverageResult,
    RmsResult,
    MinMaxResult,
    NoiseResult,
    SignalQualityResult
>;

Use std::flat_map<ChannelId, ChannelMetadata> (C++23) for the channel registry in consumer and client — better cache locality than std::unordered_map for ≤12 channels.

Important Processing Rule

DO NOT reread data from SQLite after commit.

Instead:

persist batch
emit committed in-memory batch event
process in-memory batch
stream processed batch

SQLite remains authoritative,
but hot-path processing remains memory-resident.

SQLite Requirements

SQLite is always active.

Required:

WAL mode
prepared statements (numbered parameters ?1, ?2, ?3 — not named — to avoid lookup overhead on every bind)
batched inserts
asynchronous persistence thread
transaction batching

Avoid:

per-sample transactions
Persistence Configuration

struct PersistenceConfig {
    int batch_interval_ms  = 100;    // tunable transaction boundary
    int busy_timeout_ms    = 5000;
    int wal_autocheckpoint = 0;      // disable auto; use background checkpoint thread
    int page_size          = 8192;
    int cache_size_kb      = 65536;  // 64 MB
};

Database Initialization Sequence

Execute in this exact order after sqlite3_open:

1. PRAGMA journal_mode = WAL  — read back result and assert it equals "wal"
2. PRAGMA synchronous = NORMAL
3. PRAGMA page_size = <config.page_size>
4. PRAGMA cache_size = <-config.cache_size_kb>
5. PRAGMA wal_autocheckpoint = 0
6. sqlite3_busy_timeout(db, config.busy_timeout_ms)
7. CREATE TABLE / CREATE INDEX if not exist
8. Check/insert schema_version table

WAL Checkpoint: run sqlite3_wal_checkpoint_v2(db, NULL, SQLITE_CHECKPOINT_PASSIVE, NULL, NULL) from a dedicated background thread every 5 seconds. Never let SQLite auto-checkpoint on the write path.

Prepared Statements: prepare once at SqliteWriter construction; rebind parameters per batch. Use RAII wrapper:

struct PreparedStatement {
    sqlite3_stmt* stmt = nullptr;
    ~PreparedStatement() { sqlite3_finalize(stmt); }
};

All sqlite3_* calls that return a result code must be checked via:

[[nodiscard]] std::expected<void, SqliteError> check_sqlite(int rc, const char* context);

Never silently ignore a non-SQLITE_OK result.

Database Schema
CREATE TABLE samples (
    timestamp_ns INTEGER NOT NULL,
    channel_id   INTEGER NOT NULL,
    value        REAL    NOT NULL
);

/* Covering index: FetchHistorical queries timestamp_ns + channel_id + value.
   A covering index avoids touching the main table entirely. */
CREATE INDEX idx_samples_covering
ON samples(timestamp_ns, channel_id, value);

/* Schema versioning: check on startup; run migrations as needed. */
CREATE TABLE IF NOT EXISTS schema_version (
    version INTEGER NOT NULL
);
Processing Responsibilities

Processing occurs ONLY after persistence.

Implement:

moving average
RMS
min/max
noise detection
signal quality metrics

Architecture must support future:

FFT
alarms
anomaly detection
ML inference
gRPC Requirements

Use:

async gRPC API
server-side streaming
Proto Definition
syntax = "proto3";

package telemetry;

message Sample {
    uint64 timestamp_ns = 1;
    uint32 channel_id   = 2;
    double value        = 3;  // double, not float — matches SQLite REAL precision
}

message Packet {
    uint64          sequence = 1;
    repeated Sample samples  = 2;
    bool            has_gap  = 3;  // set by broadcaster on sequence discontinuity; client shows gap indicator
}

message SubscribeRequest {
    repeated uint32 channels = 1;
}

message HistoricalRequest {
    uint64          start_ns         = 1;
    uint64          end_ns           = 2;
    repeated uint32 channels         = 3;
    uint64          max_duration_ns  = 4;  // server rejects requests exceeding this; prevents unbounded stream
}

service TelemetryService {
    rpc SubscribeLive(SubscribeRequest)
        returns (stream Packet);

    rpc FetchHistorical(HistoricalRequest)
        returns (stream Packet);
}
gRPC Behavior Requirements

Backpressure: Use grpc::ServerAsyncWriter. If a client's write buffer is full, drop packets for that client and log a warning. Never block the broadcast loop for all clients due to one slow consumer.

Disconnect handling: When stream->Write() returns false, remove the subscriber atomically. Use a mutex-protected std::vector<Subscriber> for the subscriber list (shared lock for broadcast reads, exclusive lock for add/remove).

Keepalive: configure on the server builder to detect silent client disconnects:

builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIME_MS, 10000);
builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 5000);
builder.AddChannelArgument(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0);

Input validation: server validates all HistoricalRequest fields (end_ns > start_ns, start_ns > 0, duration <= max_duration_ns) and all SubscribeRequest channel IDs are in range [0, channel_count). Return grpc::StatusCode::INVALID_ARGUMENT with a descriptive message on failure.

Server reflection: enable grpc::reflection::InitProtoReflectionServerBuilderPlugin() to allow grpcurl debugging without the proto file.

Proto arena allocation: use google::protobuf::Arena for Packet message construction in the hot broadcast path to avoid per-field heap allocation.
3. client

Language:

C++23

Rendering:

Raylib

The client must visually resemble a real ICU bedside monitor.

The attached reference image is the authoritative visual reference.

The UI should resemble:

Philips IntelliVue
GE CARESCAPE
Dräger Infinity
Mindray BeneVision
Client Responsibilities
receive processed telemetry
render live waveforms
support historical navigation
display multiple channels
display ICU-style numeric panels
display operational bedside monitor UI
show telemetry metrics/status
Client Structure
client/
├── app/
│   ├── main.cpp
│   ├── client_app.cpp
│   └── client_app.h
│
├── networking/
│   ├── grpc_client.cpp
│   ├── grpc_client.h
│   ├── live_stream.cpp
│   └── historical_stream.cpp
│
├── rendering/
│   ├── waveform_renderer.cpp
│   ├── waveform_renderer.h
│   ├── numeric_panel.cpp
│   ├── numeric_panel.h
│   ├── monitor_layout.cpp
│   ├── monitor_layout.h
│   ├── status_bar.cpp
│   ├── status_bar.h
│   ├── action_bar.cpp
│   ├── action_bar.h
│   ├── render_buffers.cpp
│   └── render_buffers.h
│
├── timeline/
│   ├── timeline_controller.cpp
│   ├── timeline_controller.h
│   ├── playback_state.cpp
│   └── playback_state.h
│
├── ui/
│   ├── telemetry_view.cpp
│   ├── telemetry_view.h
│   ├── channel_panel.cpp
│   └── channel_panel.h
│
├── config/
│   ├── client_config.h
│   └── render_config.h
│
├── common/
│   ├── telemetry_types.h
│   ├── math_utils.h
│   └── logging.h
│
├── tests/
│   ├── renderer_tests.cpp
│   ├── timeline_tests.cpp
│   ├── grpc_client_tests.cpp
│   └── navigation_tests.cpp
│
├── CMakeLists.txt
└── README.md
ICU Monitor UI Layout

The UI must contain 3 major regions:

+------------------------------------------------------+
| TOP STATUS BAR                                       |
+------------------------------------+-----------------+
|                                    |                 |
|                                    |                 |
|                                    |                 |
|         WAVEFORM AREA              | NUMERIC PANEL   |
|                                    |                 |
|                                    |                 |
|                                    |                 |
+------------------------------------+-----------------+
| BOTTOM ACTION / MENU BAR                             |
+------------------------------------------------------+
Top Status Bar

Purpose:

monitor metadata
patient metadata
date/time
profile information
monitor mode/status

Visual style:

thin dark strip
compact white text
subtle separators

Example:

ICU_SIM_01     Not Admitted     5 Jan 2023 14:09
Profile: Adult     Dynamic Waves
Waveform Area

The waveform area is the dominant visual region.

Characteristics:

stacked waveforms
scrolling right-to-left
continuous rendering
smooth lines
stable scaling
subtle grid

The UI must feel like:

real bedside monitor hardware
NOT:
a generic plotting dashboard
Waveform Colors

Use ICU monitor conventions:

Signal	Color
ECG	Bright green
Blood pressure	Red
CO2	Yellow
Respiration	Light blue
NIBP	Purple/Magenta
Waveform Rendering Requirements

Waveforms must:

scroll continuously
preserve on-screen history
use circular buffers
use anti-aliased lines
maintain stable frame pacing

Do NOT:

rebuild entire graphs every frame
use chart-style rendering

Use:

rolling buffers
incremental drawing
Numeric Panel

The right-side numeric panel displays:

latest measurements
current values
signal summaries

Characteristics:

large numbers
bright colors
stacked layout
high readability
strong contrast

Width:

~20–25% of screen
Numeric Panel Style

Examples:

HR
60

SpO2
98.6

Values should visually dominate.

Bottom Action Bar

The bottom action/menu bar must mimic real ICU hardware UI.

Characteristics:

dark blue/gray buttons
rectangular buttons
icon + text layout
hardware-like appearance

Example buttons:

Main View
Menu
Alarm Limits
Trend Graphs
Profiles
Patient Data

Avoid:

modern web-app styling
mobile-style UI
rounded glossy widgets
Typography

Use:

compact fonts
high readability
bold numeric emphasis
monitor-like appearance

Avoid:

decorative typography
oversized spacing
Rendering Requirements

The renderer must support:

continuous waveform scrolling
layered rendering
clipping regions
partial redraws
stable frame pacing
efficient GPU-friendly rendering

Target:

60 FPS

Recommended:

render textures
double buffering
preallocated geometry
Client Rendering Pipeline
gRPC Thread
      |
      v
Realtime Queue
      |
      v
Circular Buffers
      |
      v
Raylib Renderer
Historical Navigation

The user must:

jump to arbitrary timestamps
scroll backward/forward
inspect historical telemetry
seamlessly switch between live and historical views

Historical mode must preserve:

identical ICU monitor appearance
scrolling behavior
waveform style

It should feel like:

replaying bedside telemetry
NOT:
opening a chart viewer
Error Handling Strategy

| Layer | Mechanism |
|---|---|
| Generator callback (hot path) | No exceptions; increment atomic drop counter |
| Queue enqueue (hot path) | `[[nodiscard]] bool`; never throw |
| SQLite initialization | `std::expected<SqliteWriter, DbError>` factory |
| SQLite hot path | `[[nodiscard]] std::expected<void, SqliteError>` per operation |
| Processing engine | `std::expected<ProcessingResult, ProcessingError>` per batch |
| gRPC broadcaster | Handle `CANCELLED` by removing subscriber; log and continue |

API Annotations

Apply uniformly across all C++ hot-path APIs:

`[[nodiscard]]` on: PacketQueue::enqueue(), SqliteWriter::flush_batch(), ProcessingEngine::process_batch(), GrpcBroadcaster::broadcast()
`noexcept` on: generator callback wrapper, queue enqueue, circular buffer update
`[[unlikely]]` on: packet-drop branch, queue-full branch

Threading Requirements

Use:

std::jthread
std::stop_token
bounded queues
lock-free queues where possible

Avoid:

giant mutexes
unbounded queues
blocking hot paths

stop_token propagation: every thread loop must check stop_token.stop_requested() AND route it into blocking queue waits. Use moodycamel::ConcurrentQueue::wait_dequeue_timed with a 1ms timeout as the cooperative stop-point. Failure to propagate the token causes shutdown hangs.

False sharing prevention: every queue's head and tail pointers must be on separate cache lines:

alignas(std::hardware_destructive_interference_size) std::atomic<size_t> head_;
alignas(std::hardware_destructive_interference_size) std::atomic<size_t> tail_;

Thread priority:

Generator timing thread: real-time priority (SCHED_FIFO via pthread_setschedparam on Linux) to minimize drift.
Persistence thread: below-normal priority; it must never compete with the timing thread.

Shutdown ordering: App::shutdown() must drain the pipeline in reverse order to avoid lost data:

1. Stop generator thread (wait for in-flight callback to return)
2. Drain ingestion queue (spin until empty)
3. Flush persistence batch (commit current transaction)
4. Drain processing queue
5. Drain gRPC broadcaster queue

Client circular buffer synchronization: the gRPC receive thread (writer) and render thread (reader) share per-channel circular buffers. Writer must complete all sample writes before advancing write_pos_ with memory_order_release. Reader must load write_pos_ with memory_order_acquire before reading samples. Circular buffer size must be a power of two for bitmask indexing (index & (size - 1)) instead of modulo.
Memory Requirements

Must use:

preallocated buffers
reusable packet pools
bounded memory growth

Avoid:

allocations in hot paths
Logging

Use:

spdlog

Log:

startup/shutdown
queue pressure
packet drops
DB latency
gRPC disconnects
processing lag
Metrics

Track:

packets/sec
queue depth
persistence latency
processing latency
gRPC latency
dropped packets
client count
Testing Requirements

Use:

GoogleTest (GTest)
GoogleMock (GMock) when mocking is required

Do NOT use:

Catch2
Unity
custom frameworks

Even the C generator should be tested through GTest-based C++ tests.

Required Tests
Generator

timing_tests.cpp: inject a mock clock via function pointer replacing clock_gettime. Assert computed next_tick_ns values against expected — NO wall-clock assertions. Wall-clock assertions are flaky on loaded CI machines.

generator_tests.cpp: waveform generation, callback correctness, packet continuity, packet sizing logic, drift compensation.

simulation_tests.cpp: feed known timestamp inputs to each sim function; assert output is in physically plausible range.

Consumer

ingestion_tests.cpp: include a multi-threaded stress test — N producer threads each enqueue 10,000 packets; one consumer thread dequeues; assert total_dequeued == N * 10,000 with no duplicates. Run under TSan.

persistence_tests.cpp: open SQLite with sqlite3_open(":memory:", &db) for all unit tests (no filesystem I/O, no leftover files). Test WAL-specific behavior (checkpoint, SQLITE_BUSY retry) in a separate temp-file DB test.

processing_tests.cpp: purely functional — no threads, no queues, no timing. Feed a fixed std::vector<IngestedSample>, assert output values against hand-computed expected results.

grpc_tests.cpp: use an in-process gRPC channel (grpc::InsecureChannelCredentials to a test server started in the same process). No real network ports; no port-conflict flakiness.

replay_tests.cpp: historical replay correctness, post-commit event ordering, latency constraints.

queue_tests.cpp: fill the queue to capacity; verify the drop counter increments; verify the producer never blocks. Validates the bounded-queue contract.

Client

render_buffers tests: single-threaded only. Test push/get_view wraparound at the ring boundary directly. Do NOT start a render loop.

grpc_client_tests.cpp: mock the transport layer. Assert reconnect and gap-detection logic in isolation.

renderer_tests.cpp, timeline_tests.cpp, navigation_tests.cpp: test the underlying data logic (circular buffer state, timeline cursor math) without Raylib initialization.
Build System

Use:

CMake
C++23

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(GTest REQUIRED)

Warning flags (apply to all targets):

-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnull-dereference
-Werror in CI builds

Release optimizations:

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)  # LTO

Sanitizer build options (off by default; enable for development and CI):

option(ENABLE_ASAN "AddressSanitizer + UBSan" OFF)
option(ENABLE_TSAN "ThreadSanitizer" OFF)

Note: ASan and TSan are mutually exclusive. TSan is the most valuable build for catching memory-ordering bugs in the lock-free pipeline.

The generator C wrapper (the C++ shim that calls the C generator) must be compiled with -fno-exceptions -fno-rtti to prevent accidental exception propagation across the C boundary.

Static assertions (add to generator_internal.h and consumer/common/telemetry_types.h):

// generator_internal.h
static_assert(GENERATOR_MAX_CHANNELS <= 64,
    "channel count must fit in a uint64_t bitmask");

// consumer/common/telemetry_types.h
static_assert(std::is_trivially_copyable_v<IngestedPacket>,
    "IngestedPacket must be trivially copyable for lock-free queue");
static_assert(sizeof(GeneratorSample) % 8 == 0,
    "GeneratorSample must be 8-byte aligned for SIMD-friendly access");
Dependencies
Consumer
gRPC
protobuf
SQLite3
spdlog
moodycamel ConcurrentQueue
GTest
GMock
Client
Raylib
gRPC
protobuf
spdlog
GTest
GMock

---

Implementation Todo List

Phase 0 — Repository & Build Scaffold

- [x] Create root CMakeLists.txt with add_subdirectory for generator, consumer, client
- [x] Set CMAKE_CXX_STANDARD 23, CMAKE_CXX_STANDARD_REQUIRED, CMAKE_EXPORT_COMPILE_COMMANDS
- [x] Add -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnull-dereference to all targets
- [x] Add ENABLE_ASAN and ENABLE_TSAN CMake options wired to compile/link flags
- [x] Add CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON for LTO
- [x] Verify CMake finds: gRPC, protobuf, SQLite3, spdlog, moodycamel, GTest, Raylib
- [x] Add .gitignore (build/, *.db, compile_commands.json)
- [x] Write root README.md covering build instructions for Linux and WSL

Phase 1 — Generator

Infrastructure

- [x] Create generator/CMakeLists.txt (static lib target + test executable)
- [x] Write generator/generator.h — full public API as defined in the plan
- [x] Write generator/generator_internal.h — internal timing state, packet pool structs
- [x] Add static_assert(GENERATOR_MAX_CHANNELS <= 64) in generator_internal.h

Core Implementation

- [x] Implement generator/generator.c
  - [x] generator_config_validate(): all checks (non-null sim_fn, rate bounds, channel count)
  - [x] generator_start(): spawn timing thread, allocate packet pool, return GeneratorError
  - [x] generator_stop(): signal thread, join, return GeneratorError
  - [x] Timing loop: clock_gettime(CLOCK_MONOTONIC) + nanosleep() with drift compensation
  - [x] Packetization: compute samples_per_packet from sample_rate / packets_per_second dynamically
  - [x] Per-packet: call simulation_fn for each channel sample, apply noise, handle jitter
  - [x] Packet pool: pre-allocate GENERATOR_MAX_CHANNELS * 2 packets at startup — zero malloc in timing loop
  - [x] Drop logic: check packet_drop_probability; set has_gap on next packet if dropped

Simulation Functions

- [x] Write generator/generator_simulations.h and generator_simulations.c
  - [x] generator_sim_ecg(): realistic PQRST waveform (parameterized by HR)
  - [x] generator_sim_spo2(): sinusoidal approximation with perfusion index
  - [x] generator_sim_bp(): arterial pressure waveform (systolic/diastolic configurable)
  - [x] generator_sim_eeg(): bandlimited noise with alpha/beta/theta component mix
  - [x] generator_sim_noise(): Gaussian white noise
  - [x] generator_sim_flatline(): constant 0.0

Tests

- [x] generator/tests/timing_tests.cpp: mock clock via function pointer; assert next_tick_ns values — no wall-clock assertions
- [x] generator/tests/generator_tests.cpp: callback correctness, packet continuity, sequence monotonicity, packet sizing from config
- [x] generator/tests/simulation_tests.cpp: each sim function output is within physically plausible range for known inputs

Phase 2 — Consumer

Infrastructure

- [x] Create consumer/CMakeLists.txt (executable + test executable, proto codegen target)
- [x] Generate gRPC/protobuf C++ from consumer/grpc/telemetry.proto
- [x] Write consumer/common/telemetry_types.h: ChannelId, IngestedPacket, CommittedBatch, static_assert checks
- [x] Write consumer/common/timestamps.h: monotonic clock helpers
- [x] Write consumer/common/logging.h: spdlog setup (file + console sinks)
- [x] Write consumer/common/thread_utils.h: set_thread_priority() wrapper (SCHED_FIFO on Linux)
- [x] Write consumer/config/consumer_config.h: ConsumerConfig, PersistenceConfig, GeneratorConfig wiring

Ingestion

- [x] Write consumer/ingestion/packet_queue.h + .cpp
  - [x] Wrap moodycamel::ConcurrentQueue<IngestedPacket> with bounded capacity
  - [x] [[nodiscard]] bool enqueue(IngestedPacket&&) noexcept — increment drop counter on full
  - [x] wait_dequeue_timed() with stop_token cooperative exit (1ms timeout loop)
  - [x] Expose std::atomic<uint64_t> drop_count for metrics
- [x] Write consumer/ingestion/ingestion_manager.h + .cpp
  - [x] Generator packet callback: memcpy samples into IngestedPacket, enqueue, return immediately — noexcept
  - [x] Wire to PacketQueue; log queue depth at configurable intervals

Persistence

- [x] Write consumer/persistence/sqlite_writer.h + .cpp
  - [x] std::expected<SqliteWriter, DbError> factory (static open())
  - [x] Database init sequence: WAL, synchronous=NORMAL, page_size, cache_size, wal_autocheckpoint=0, busy_timeout
  - [x] Read back PRAGMA journal_mode result; assert equals "wal"
  - [x] CREATE TABLE samples + covering index + schema_version table
  - [x] PreparedStatement RAII wrapper; prepare INSERT once at construction
  - [x] [[nodiscard]] std::expected<void, SqliteError> flush_batch(std::span<const IngestedPacket>)
  - [x] Transaction batching: accumulate until batch_interval_ms or batch_size threshold
  - [x] check_sqlite() helper used for every sqlite3_* call
  - [x] Background WAL checkpoint thread: SQLITE_CHECKPOINT_PASSIVE every 5 seconds
- [x] Write consumer/persistence/persistence_events.h + .cpp
  - [x] Post-commit event queue: moodycamel::ConcurrentQueue<CommittedBatch>
  - [x] After each successful flush_batch, move batch into CommittedBatch and enqueue

Processing

- [x] Write consumer/processing/signal_metrics.h + .cpp
  - [x] moving_average(std::span<const double> samples, size_t window) -> double
  - [x] rms(std::span<const double> samples) -> double
  - [x] min_max(std::span<const double> samples) -> std::pair<double,double>
  - [x] noise_level(std::span<const double> samples) -> double
  - [x] signal_quality(std::span<const double> samples) -> double
  - [x] All functions are pure, noexcept, take std::span<const double>
- [x] Write consumer/processing/processing_engine.h + .cpp
  - [x] Consumes CommittedBatch from persistence_events queue
  - [x] Partition batch by ChannelId using std::flat_map
  - [x] Run per-channel metrics; produce ProcessingResult (std::variant)
  - [x] [[nodiscard]] std::expected<ProcessingResult, ProcessingError> process_batch(...)
  - [x] Enqueue results to processed event queue for gRPC broadcast

gRPC

- [x] Write consumer/grpc/telemetry_service.h + .cpp
  - [x] Implement TelemetryService::SubscribeLive: validate channels, add subscriber, stream via ServerAsyncWriter
  - [x] Implement TelemetryService::FetchHistorical: validate time range and max_duration_ns, query SQLite, stream results
  - [x] Return INVALID_ARGUMENT for out-of-range channel IDs or invalid time ranges
- [x] Write consumer/grpc/grpc_broadcaster.h + grpc_broadcaster.cpp
  - [x] [[nodiscard]] bool broadcast(const ProcessedBatch&) noexcept — serialize with proto Arena
  - [x] Per-client backpressure: drop and log if write buffer full; never block broadcast loop
  - [x] On Write() == false: remove subscriber atomically (exclusive lock on subscriber list)
  - [x] Set has_gap=true on Packet when sequence discontinuity detected
  - [x] Configure keepalive args on ServerBuilder
  - [x] Enable server reflection (grpc::reflection::InitProtoReflectionServerBuilderPlugin)

App Wiring

- [x] Write consumer/app/app.h + app.cpp: start all threads, wire callbacks, implement shutdown() drain sequence
- [x] Write consumer/app/main.cpp: parse config, construct App, block on signal (SIGINT/SIGTERM)

Consumer Tests

- [x] consumer/tests/ingestion_tests.cpp: multi-threaded stress (N producers × 10,000 packets), assert no duplicates — run under TSan
- [x] consumer/tests/queue_tests.cpp: fill queue to capacity, verify drop counter increments, verify no blocking
- [x] consumer/tests/persistence_tests.cpp: in-memory SQLite; test flush_batch, WAL mode assertion, schema_version, SQLITE_BUSY retry
- [x] consumer/tests/processing_tests.cpp: purely functional; fixed input → assert expected output values
- [x] consumer/tests/grpc_tests.cpp: in-process gRPC server; test SubscribeLive and FetchHistorical with mock data
- [x] consumer/tests/replay_tests.cpp: historical replay correctness, post-commit event ordering

Phase 3 — Client

Infrastructure

- [x] Create client/CMakeLists.txt (executable + test executable)
- [x] Write client/common/telemetry_types.h (shared with consumer via symlink or copy)
- [x] Write client/common/logging.h: spdlog client setup
- [x] Write client/config/client_config.h: server address, channel count, render dimensions
- [x] Write client/config/render_config.h: waveform colors, panel dimensions, font sizes

Networking

- [x] Write client/networking/grpc_client.h + .cpp: connect to TelemetryService, handle reconnect with exponential backoff
- [x] Write client/networking/live_stream.cpp: SubscribeLive RPC, push IngestedPackets to Realtime Queue
- [x] Write client/networking/historical_stream.cpp: FetchHistorical RPC, push to replay queue

Render Buffers

- [x] Write client/rendering/render_buffers.h + .cpp
  - [x] Per-channel circular buffer: std::array<double, N> where N is power-of-two
  - [x] write_pos_ as std::atomic<size_t> with memory_order_release on write, memory_order_acquire on read
  - [x] push(ChannelId, double value) noexcept: write sample, advance write_pos_
  - [x] get_view(ChannelId) -> std::span<const double>: returns ordered view for rendering

Rendering

- [x] Write client/rendering/waveform_renderer.h + .cpp
  - [x] Pre-allocate std::array<Vector2, WAVEFORM_WIDTH_PIXELS> per channel at startup
  - [x] Per-frame: compute only N new points added since last frame (incremental, not full rebuild)
  - [x] DrawLineStrip with anti-aliasing; clip to waveform area
  - [x] Subtle grid overlay (horizontal baselines, vertical time tick every second)
- [x] Write client/rendering/numeric_panel.h + .cpp: large-font current values per channel, stacked layout, color-coded
- [x] Write client/rendering/monitor_layout.h + .cpp: compute waveform area rect, numeric panel rect, status/action bar rects from window size
- [x] Write client/rendering/status_bar.h + .cpp: patient ID, date/time, profile, monitor mode
- [x] Write client/rendering/action_bar.h + .cpp: hardware-style rectangular buttons (Main View, Menu, Alarm Limits, Trend Graphs, Profiles, Patient Data)
- [x] Use RenderTexture2D for waveform area to support partial redraws and stable frame pacing

Timeline

- [x] Write client/timeline/playback_state.h + .cpp: live vs historical mode, current timestamp, playback speed
- [x] Write client/timeline/timeline_controller.h + .cpp: jump-to-timestamp, scroll forward/backward, switch live↔historical

UI

- [x] Write client/ui/channel_panel.h + .cpp: per-channel label, waveform sub-region, color assignment
- [x] Write client/ui/telemetry_view.h + .cpp: compose all panels; route render buffer data to waveform_renderer

App Wiring

- [x] Write client/app/client_app.h + .cpp: InitWindow (Raylib), start gRPC thread, main render loop at 60 FPS, shutdown on WindowShouldClose
- [x] Write client/app/main.cpp: parse config, construct ClientApp, run loop

Client Tests

- [x] client/tests/render_buffers_tests.cpp: single-threaded; test push/get_view wraparound at ring boundary
- [x] client/tests/grpc_client_tests.cpp: mock transport; assert reconnect logic and gap-detection
- [x] client/tests/timeline_tests.cpp: timestamp cursor math, live↔historical transition
- [x] client/tests/navigation_tests.cpp: historical scroll bounds, jump-to-timestamp accuracy

Phase 4 — Integration & Validation

- [x] End-to-end smoke test: start consumer, start generator, connect client — verify waveforms render within 500ms UI latency target
- [x] Run full pipeline under TSan for 60 seconds; assert zero data race reports
- [x] Run full pipeline under ASan+UBSan; assert zero heap errors
- [x] Verify WAL file stays bounded under sustained load (checkpoint thread active)
- [x] Test graceful shutdown: SIGTERM → all queues drain → DB closes cleanly → no lost samples
- [x] Test client reconnect: kill and restart consumer; client must reconnect and resume without crash
- [x] Test historical navigation: seek to 30 minutes prior; assert correct waveforms loaded from SQLite
- [x] Measure and log actual UI latency at steady state; assert < 500ms