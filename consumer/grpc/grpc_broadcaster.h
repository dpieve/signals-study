#pragma once
#include <shared_mutex>
#include <vector>
#include <cstdint>
#include <grpcpp/grpcpp.h>
#include "telemetry.grpc.pb.h"
#include "../common/telemetry_types.h"

/// Describes a single live subscriber.
struct Subscriber {
    grpc::ServerWriter<telemetry::Packet>* writer;
    std::vector<uint32_t>                  channels; ///< empty = all channels
};

/// Thread-safe broadcaster that fans out committed batches to all live gRPC
/// streaming subscribers.
///
/// add_subscriber() / remove_subscriber() may be called from any thread.
/// broadcast() is called from the processing thread; it takes a shared lock
/// so multiple callers can broadcast concurrently.
class GrpcBroadcaster {
public:
    GrpcBroadcaster() = default;

    /// Register a new subscriber.  Must be called before the RPC handler
    /// blocks on the stream (typically from the gRPC thread pool).
    void add_subscriber(Subscriber s);

    /// Deregister a subscriber by its writer pointer.
    void remove_subscriber(grpc::ServerWriter<telemetry::Packet>* writer);

    /// Broadcast @p batch to all matching subscribers.
    /// Subscribers whose Write() returns false are removed after iteration.
    /// @return true if at least one subscriber received data; false otherwise.
    [[nodiscard]] bool broadcast(const CommittedBatch& batch) noexcept;

private:
    std::shared_mutex      subscribers_mutex_;
    std::vector<Subscriber> subscribers_;

    // Track the last sequence number to detect gaps.
    uint64_t last_sequence_{0};
    bool     first_packet_{true};
};
