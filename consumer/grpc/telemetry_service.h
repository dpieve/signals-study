#pragma once
#include <sqlite3.h>
#include <grpcpp/grpcpp.h>
#include "telemetry.grpc.pb.h"
#include "grpc_broadcaster.h"
#include "../config/consumer_config.h"

/// Concrete implementation of the TelemetryService gRPC service.
///
/// SubscribeLive:    registers the caller as a live subscriber and blocks
///                   until the stream is cancelled.
/// FetchHistorical:  queries SQLite directly and streams matching rows.
class TelemetryServiceImpl final : public telemetry::TelemetryService::Service {
public:
    /// @param broadcaster  Shared broadcaster (owned by App).
    /// @param db           Read-only SQLite handle for historical queries.
    ///                     Must outlive this object.
    /// @param cfg          Consumer configuration (channel_count, etc.).
    TelemetryServiceImpl(GrpcBroadcaster& broadcaster,
                         sqlite3*         db,
                         const ConsumerConfig& cfg);

    grpc::Status SubscribeLive(
        grpc::ServerContext*                 ctx,
        const telemetry::SubscribeRequest*   req,
        grpc::ServerWriter<telemetry::Packet>* writer) override;

    grpc::Status FetchHistorical(
        grpc::ServerContext*                  ctx,
        const telemetry::HistoricalRequest*   req,
        grpc::ServerWriter<telemetry::Packet>* writer) override;

private:
    GrpcBroadcaster&     broadcaster_;
    sqlite3*             db_;
    const ConsumerConfig& cfg_;

    /// Validate that every channel ID in @p channels is in [0, channel_count).
    grpc::Status validate_channels(
        const google::protobuf::RepeatedField<uint32_t>& channels) const;
};
