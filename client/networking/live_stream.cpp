/// live_stream.cpp
///
/// SubscribeLive RPC implementation detail.
///
/// The actual streaming loop lives in GrpcClient::stream_loop() (grpc_client.cpp)
/// which already handles connection, backoff, and packet dispatch.
///
/// This file documents the RPC contract and provides the helper used by tests
/// and future refactoring to isolate the subscription request construction.

#include "grpc_client.h"
#include "../common/logging.h"
#include "telemetry.pb.h"
#include "telemetry.grpc.pb.h"

/// Build a SubscribeRequest from a list of channel IDs.
telemetry::SubscribeRequest make_subscribe_request(
        const std::vector<uint32_t>& channels) {
    telemetry::SubscribeRequest req;
    for (const uint32_t ch : channels) {
        req.add_channels(ch);
    }
    return req;
}
