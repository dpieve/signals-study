# ICU Real-Time Telemetry Simulator — Consumer

Receives packets from the C generator library, persists them to SQLite, computes signal metrics, and broadcasts live telemetry over gRPC.

## Architecture

```
Generator → PacketQueue → Persistence Thread → SQLite
                                              ↓ (post-commit event)
                                    Processing Engine → GrpcBroadcaster → Client
```

Data is persisted before processing. SQLite is authoritative.

## Dependencies

| Library | Purpose |
|---------|---------|
| SQLite3 | On-disk telemetry store (WAL mode) |
| spdlog | Structured logging |
| moodycamel ConcurrentQueue | Lock-free packet and event queues |
| gRPC / Protobuf | Live-stream and historical-replay RPC |
| GoogleTest / GMock | Unit tests |

Requires a C++23-capable compiler (GCC 13+ or Clang 17+).

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Run

```bash
./build/consumer [options]

  --db, -d <path>        SQLite database path  (default: telemetry.db)
  --port, -p <port>      gRPC listening port   (default: 50051)
  --channels, -c <n>     Number of ICU channels (default: 4)
  --sample-rate <hz>     Samples per second     (default: 44100)
  --log, -l <file>       Write logs to file
```

Send SIGINT or SIGTERM to initiate a clean shutdown.

## Test

```bash
cd build && ctest --output-on-failure
```

Or run the binary directly:

```bash
./build/consumer_tests
```

## Module overview

| Path | Description |
|------|-------------|
| `common/` | Shared types, clock helpers, logging, thread utilities |
| `config/` | `ConsumerConfig` and `PersistenceConfig` structs |
| `ingestion/` | `PacketQueue` (bounded lock-free queue) + `IngestionManager` (C callback bridge) |
| `persistence/` | `SqliteWriter` (WAL batched inserts) + `PersistenceEvents` (post-commit bus) |
| `processing/` | `signal_metrics` (pure functions) + `ProcessingEngine` (per-channel history) |
| `grpc/` | Proto definition, `TelemetryServiceImpl`, `GrpcBroadcaster` |
| `app/` | `App` (owns all components), `main.cpp` |
| `tests/` | GoogleTest unit tests for each layer |
