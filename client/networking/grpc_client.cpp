#include "grpc_client.h"
#include "../common/logging.h"
#include <chrono>
#include <thread>
#include <stop_token>

GrpcClient::GrpcClient(const std::string& address)
    : address_(address) {
    channel_ = grpc::CreateChannel(address_, grpc::InsecureChannelCredentials());
    stub_    = telemetry::TelemetryService::NewStub(channel_);
    spdlog::info("[grpc_client] Created stub for {}", address_);
}

GrpcClient::~GrpcClient() {
    stop();
}

void GrpcClient::start_live_stream(
        const std::vector<uint32_t>&                  channels,
        std::function<void(const telemetry::Packet&)> on_packet) {
    subscribed_channels_ = channels;
    on_packet_cb_        = std::move(on_packet);
    stop_requested_.store(false, std::memory_order_release);

    stream_thread_ = std::jthread(
        [this](std::stop_token stoken) { stream_loop(std::move(stoken)); });
}

void GrpcClient::stop() {
    stop_requested_.store(true, std::memory_order_release);
    if (stream_thread_.joinable()) {
        stream_thread_.request_stop();
        stream_thread_.join();
    }
    connected_.store(false, std::memory_order_release);
}

void GrpcClient::stream_loop(std::stop_token stoken) {
    // Exponential back-off: start at 100 ms, double each failure, cap at 5 s.
    constexpr auto kBackoffMin = std::chrono::milliseconds(100);
    constexpr auto kBackoffMax = std::chrono::milliseconds(5000);
    auto backoff = kBackoffMin;

    while (!stoken.stop_requested() &&
           !stop_requested_.load(std::memory_order_acquire)) {
        // Build the subscription request
        telemetry::SubscribeRequest req;
        for (const uint32_t ch : subscribed_channels_) {
            req.add_channels(ch);
        }

        grpc::ClientContext ctx;
        // Register a stop callback: when the jthread's stop_source fires,
        // cancel the gRPC context so the blocking Read() call returns.
        std::stop_callback stop_cb(stoken, [&ctx]() noexcept {
            ctx.TryCancel();
        });

        auto reader = stub_->SubscribeLive(&ctx, req);
        connected_.store(true, std::memory_order_release);
        backoff = kBackoffMin; // reset back-off on successful connect
        spdlog::info("[grpc_client] Subscribed to live stream ({} channels)",
                     subscribed_channels_.size());

        telemetry::Packet pkt;
        while (reader->Read(&pkt)) {
            if (stoken.stop_requested()) break;
            if (on_packet_cb_) {
                on_packet_cb_(pkt);
            }
        }

        connected_.store(false, std::memory_order_release);
        const grpc::Status status = reader->Finish();

        if (stoken.stop_requested() ||
            stop_requested_.load(std::memory_order_acquire)) {
            break;
        }

        if (status.error_code() == grpc::StatusCode::CANCELLED) {
            break; // clean shutdown requested
        }

        spdlog::warn("[grpc_client] Stream ended: {} — retrying in {} ms",
                     status.error_message(),
                     std::chrono::duration_cast<std::chrono::milliseconds>(backoff).count());

        // Sleep interruptibly — wake early if stop is requested
        const auto deadline = std::chrono::steady_clock::now() + backoff;
        while (std::chrono::steady_clock::now() < deadline) {
            if (stoken.stop_requested()) goto exit_loop;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Double back-off, cap at maximum
        backoff = std::min(backoff * 2, kBackoffMax);
    }

exit_loop:
    connected_.store(false, std::memory_order_release);
    spdlog::info("[grpc_client] Stream loop exited");
}
