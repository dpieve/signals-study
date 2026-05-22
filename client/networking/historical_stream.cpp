/// historical_stream.cpp
///
/// FetchHistorical RPC — blocking synchronous implementation.
///
/// Reads the complete historical packet stream for a given time range and
/// delivers every packet in order to the provided callback.
/// Intended to be called from a dedicated background thread (not the render thread).

#include "../common/logging.h"
#include "telemetry.pb.h"
#include "telemetry.grpc.pb.h"
#include <functional>
#include <vector>
#include <string>
#include <grpcpp/grpcpp.h>

/// Fetch historical telemetry packets from the consumer server.
///
/// @param stub       Connected TelemetryService stub
/// @param start_ns   Start of the time window (ns since epoch)
/// @param end_ns     End of the time window
/// @param channels   Channel IDs to include
/// @param on_packet  Callback invoked for each received packet (in order)
/// @return           gRPC status (OK on success)
grpc::Status fetch_historical(
        telemetry::TelemetryService::Stub&             stub,
        uint64_t                                       start_ns,
        uint64_t                                       end_ns,
        const std::vector<uint32_t>&                   channels,
        std::function<void(const telemetry::Packet&)>  on_packet) {
    telemetry::HistoricalRequest req;
    req.set_start_ns(start_ns);
    req.set_end_ns(end_ns);
    req.set_max_duration_ns(end_ns - start_ns);
    for (const uint32_t ch : channels) {
        req.add_channels(ch);
    }

    grpc::ClientContext ctx;
    auto reader = stub.FetchHistorical(&ctx, req);

    telemetry::Packet pkt;
    size_t count = 0;
    while (reader->Read(&pkt)) {
        on_packet(pkt);
        ++count;
    }

    const grpc::Status status = reader->Finish();
    if (status.ok()) {
        spdlog::info("[historical_stream] Received {} packets for [{}, {})",
                     count, start_ns, end_ns);
    } else {
        spdlog::error("[historical_stream] RPC failed: {}", status.error_message());
    }
    return status;
}
