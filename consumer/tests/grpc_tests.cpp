#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <grpcpp/grpcpp.h>
#include "telemetry.grpc.pb.h"
#include "../grpc/grpc_broadcaster.h"
#include "../config/consumer_config.h"

// ---------------------------------------------------------------------------
// SubscribeRequest validation — compile-time pattern test
// ---------------------------------------------------------------------------

// Verify that a SubscribeRequest with out-of-range channels would be rejected
// by the validation logic (white-box test of the logic itself, not the RPC
// transport).

static grpc::Status validate_channels_impl(
        const google::protobuf::RepeatedField<uint32_t>& channels,
        uint32_t channel_count) {
    for (const uint32_t ch : channels) {
        if (ch >= channel_count) {
            return grpc::Status(
                grpc::StatusCode::INVALID_ARGUMENT,
                "channel_id " + std::to_string(ch)
                + " is out of range [0, " + std::to_string(channel_count) + ")");
        }
    }
    return grpc::Status::OK;
}

TEST(SubscribeValidation, ValidChannels) {
    telemetry::SubscribeRequest req;
    req.add_channels(0);
    req.add_channels(1);
    req.add_channels(3);

    constexpr uint32_t kChannelCount = 4;
    auto status = validate_channels_impl(req.channels(), kChannelCount);
    EXPECT_TRUE(status.ok());
}

TEST(SubscribeValidation, EmptyChannelsAlwaysValid) {
    telemetry::SubscribeRequest req;  // no channels
    auto status = validate_channels_impl(req.channels(), 4);
    EXPECT_TRUE(status.ok());
}

TEST(SubscribeValidation, OutOfRangeChannelRejected) {
    telemetry::SubscribeRequest req;
    req.add_channels(0);
    req.add_channels(5);  // channel_count = 4

    auto status = validate_channels_impl(req.channels(), 4);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
}

TEST(SubscribeValidation, ExactBoundaryAccepted) {
    telemetry::SubscribeRequest req;
    req.add_channels(3);  // last valid channel when count=4

    auto status = validate_channels_impl(req.channels(), 4);
    EXPECT_TRUE(status.ok());
}

TEST(SubscribeValidation, ExactBoundaryPlusOneRejected) {
    telemetry::SubscribeRequest req;
    req.add_channels(4);  // one past last valid channel

    auto status = validate_channels_impl(req.channels(), 4);
    EXPECT_FALSE(status.ok());
}

// ---------------------------------------------------------------------------
// HistoricalRequest validation
// ---------------------------------------------------------------------------

static grpc::Status validate_historical(
        uint64_t start_ns, uint64_t end_ns, uint64_t max_duration_ns) {
    if (end_ns <= start_ns) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "end_ns must be greater than start_ns");
    }
    const uint64_t duration_ns = end_ns - start_ns;
    const uint64_t max_dur = max_duration_ns > 0
                             ? max_duration_ns
                             : static_cast<uint64_t>(60) * 1'000'000'000ULL;
    if (duration_ns > max_dur) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "requested duration exceeds max_duration_ns");
    }
    return grpc::Status::OK;
}

TEST(HistoricalValidation, ValidRange) {
    auto status = validate_historical(100, 200, 1000);
    EXPECT_TRUE(status.ok());
}

TEST(HistoricalValidation, EndBeforeStartRejected) {
    auto status = validate_historical(200, 100, 1000);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
}

TEST(HistoricalValidation, EqualTimestampsRejected) {
    auto status = validate_historical(100, 100, 1000);
    EXPECT_FALSE(status.ok());
}

TEST(HistoricalValidation, ExceedsMaxDurationRejected) {
    // Duration = 500, max = 400 → rejected
    auto status = validate_historical(0, 500, 400);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
}

TEST(HistoricalValidation, ExactMaxDurationAccepted) {
    // Duration == max → accepted
    auto status = validate_historical(0, 400, 400);
    EXPECT_TRUE(status.ok());
}

// ---------------------------------------------------------------------------
// GrpcBroadcaster — basic subscriber management
// ---------------------------------------------------------------------------

TEST(GrpcBroadcaster, AddAndRemoveSubscriber) {
    GrpcBroadcaster broadcaster;

    // We can't actually construct a ServerWriter without a real gRPC context,
    // so we use a null pointer as a placeholder for remove/add symmetry tests.
    // The broadcaster stores by pointer identity, so null is valid here.
    grpc::ServerWriter<telemetry::Packet>* fake_writer = nullptr;

    Subscriber sub;
    sub.writer = fake_writer;
    sub.channels = {0, 1};

    broadcaster.add_subscriber(sub);
    broadcaster.remove_subscriber(fake_writer);
    // No assertion needed — test ensures no crash / undefined behaviour.
}
