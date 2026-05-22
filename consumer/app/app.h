#pragma once
#include <atomic>
#include <memory>
#include <thread>
#include "../config/consumer_config.h"
#include "../ingestion/packet_queue.h"
#include "../ingestion/ingestion_manager.h"
#include "../persistence/sqlite_writer.h"
#include "../persistence/persistence_events.h"
#include "../processing/processing_engine.h"
#include "../grpc/grpc_broadcaster.h"

// Forward declarations to avoid pulling in heavy gRPC headers in app.h.
namespace grpc { class Server; }
class TelemetryServiceImpl;

/// Top-level application object.  Owns all components and threads.
///
/// Lifecycle:
///   App app(cfg);
///   app.run();    // blocks until shutdown() is called
///   app.shutdown();
///
/// Shutdown order:
///   1. Stop generator
///   2. Drain ingestion queue
///   3. Flush persistence (last batch)
///   4. Drain processing engine
///   5. Drain gRPC broadcaster
///   6. Stop gRPC server
class App {
public:
    explicit App(ConsumerConfig cfg);
    ~App();

    /// Start all threads and block until shutdown() is called.
    void run();

    /// Signal the application to stop.  Safe to call from a signal handler
    /// (uses only atomic store and a flag).
    void shutdown() noexcept;

private:
    void persistence_loop(std::stop_token st);
    void processing_loop(std::stop_token st);

    ConsumerConfig cfg_;

    // Components
    PacketQueue       queue_;
    IngestionManager  ingestion_mgr_;
    PersistenceEvents events_;
    ProcessingEngine  engine_;
    GrpcBroadcaster   broadcaster_;

    // SQLite handle shared with the gRPC service for historical queries.
    sqlite3* shared_db_{nullptr};

    // Threads
    std::jthread persistence_thread_;
    std::jthread processing_thread_;

    // gRPC
    std::unique_ptr<grpc::Server>           grpc_server_;
    std::unique_ptr<TelemetryServiceImpl>   telemetry_service_;

    std::atomic<bool> running_{false};
};
