#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <grpcpp/grpcpp.h>
#include "telemetry.grpc.pb.h"

/// Manages the connection to the consumer's TelemetryService gRPC server.
///
/// Runs a dedicated background jthread that:
///   - Calls SubscribeLive and reads the server-streaming response
///   - On any failure: applies exponential back-off (100 ms → 5 s) and retries
///   - Calls on_packet callback for every received Packet (from the bg thread)
///
/// Thread safety: start_live_stream / stop may be called from any thread.
class GrpcClient {
public:
    explicit GrpcClient(const std::string& address);
    ~GrpcClient();

    // Non-copyable
    GrpcClient(const GrpcClient&)            = delete;
    GrpcClient& operator=(const GrpcClient&) = delete;

    /// Start the background stream thread.
    /// @p channels  list of channel IDs to subscribe to (uint32_t values)
    /// @p on_packet callback invoked on the gRPC background thread for each Packet
    void start_live_stream(const std::vector<uint32_t>&                     channels,
                           std::function<void(const telemetry::Packet&)>    on_packet);

    /// Signal the background thread to stop and block until it exits.
    void stop();

    [[nodiscard]] bool is_connected() const noexcept {
        return connected_.load(std::memory_order_acquire);
    }

private:
    std::string                                   address_;
    std::shared_ptr<grpc::Channel>                channel_;
    std::unique_ptr<telemetry::TelemetryService::Stub> stub_;

    std::jthread                                  stream_thread_;
    std::atomic<bool>                             connected_{false};
    std::atomic<bool>                             stop_requested_{false};

    // Populated in start_live_stream; read by the background thread.
    std::vector<uint32_t>                         subscribed_channels_;
    std::function<void(const telemetry::Packet&)> on_packet_cb_;

    void stream_loop(std::stop_token stoken);
};
