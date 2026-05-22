#include "telemetry_service.h"
#include <chrono>
#include <thread>
#include <spdlog/spdlog.h>
#include <google/protobuf/arena.h>

static constexpr uint64_t kMaxHistoricalDurationNs =
    static_cast<uint64_t>(60) * 1'000'000'000ULL; // 60 seconds default cap

TelemetryServiceImpl::TelemetryServiceImpl(GrpcBroadcaster&      broadcaster,
                                           sqlite3*              db,
                                           const ConsumerConfig& cfg)
    : broadcaster_(broadcaster), db_(db), cfg_(cfg) {}

// ---------------------------------------------------------------------------
// Validation helper
// ---------------------------------------------------------------------------

grpc::Status TelemetryServiceImpl::validate_channels(
        const google::protobuf::RepeatedField<uint32_t>& channels) const {
    for (const uint32_t ch : channels) {
        if (ch >= cfg_.channel_count) {
            return grpc::Status(
                grpc::StatusCode::INVALID_ARGUMENT,
                "channel_id " + std::to_string(ch) + " is out of range [0, "
                + std::to_string(cfg_.channel_count) + ")");
        }
    }
    return grpc::Status::OK;
}

// ---------------------------------------------------------------------------
// SubscribeLive
// ---------------------------------------------------------------------------

grpc::Status TelemetryServiceImpl::SubscribeLive(
        grpc::ServerContext*                  ctx,
        const telemetry::SubscribeRequest*    req,
        grpc::ServerWriter<telemetry::Packet>* writer) {
    if (auto s = validate_channels(req->channels()); !s.ok()) { return s; }

    Subscriber sub;
    sub.writer = writer;
    sub.channels.assign(req->channels().begin(), req->channels().end());

    broadcaster_.add_subscriber(sub);
    spdlog::info("SubscribeLive: new subscriber, channels={}",
                 req->channels().size());

    // Block until the client disconnects (context is cancelled).
    while (!ctx->IsCancelled()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    broadcaster_.remove_subscriber(writer);
    spdlog::info("SubscribeLive: subscriber disconnected");
    return grpc::Status::OK;
}

// ---------------------------------------------------------------------------
// FetchHistorical
// ---------------------------------------------------------------------------

grpc::Status TelemetryServiceImpl::FetchHistorical(
        grpc::ServerContext*                   ctx,
        const telemetry::HistoricalRequest*    req,
        grpc::ServerWriter<telemetry::Packet>* writer) {
    // Validate time range.
    if (req->end_ns() <= req->start_ns()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "end_ns must be greater than start_ns");
    }

    const uint64_t duration_ns = req->end_ns() - req->start_ns();
    const uint64_t max_dur = req->max_duration_ns() > 0
                             ? req->max_duration_ns()
                             : kMaxHistoricalDurationNs;
    if (duration_ns > max_dur) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "requested duration exceeds max_duration_ns");
    }

    if (auto s = validate_channels(req->channels()); !s.ok()) { return s; }

    // Build query.  If no channels specified, return all.
    std::string sql =
        "SELECT timestamp_ns, channel_id, value FROM samples "
        "WHERE timestamp_ns >= ?1 AND timestamp_ns < ?2 ";
    if (!req->channels().empty()) {
        sql += "AND channel_id IN (";
        for (int i = 0; i < req->channels_size(); ++i) {
            if (i > 0) { sql += ','; }
            sql += std::to_string(req->channels(i));
        }
        sql += ") ";
    }
    sql += "ORDER BY timestamp_ns ASC;";

    sqlite3_stmt* stmt = nullptr;
    const int prc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (prc != SQLITE_OK) {
        return grpc::Status(grpc::StatusCode::INTERNAL,
                            std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(req->start_ns()));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(req->end_ns()));

    // Stream rows in batches of up to 512 samples per Packet.
    static constexpr int kBatchSize = 512;
    google::protobuf::Arena arena;
    auto* pkt = google::protobuf::Arena::CreateMessage<telemetry::Packet>(&arena);
    uint64_t seq = 0;

    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (ctx->IsCancelled()) { break; }

        auto* s = pkt->add_samples();
        s->set_timestamp_ns(static_cast<uint64_t>(
            sqlite3_column_int64(stmt, 0)));
        s->set_channel_id(static_cast<uint32_t>(
            sqlite3_column_int64(stmt, 1)));
        s->set_value(sqlite3_column_double(stmt, 2));

        if (pkt->samples_size() >= kBatchSize) {
            pkt->set_sequence(seq++);
            if (!writer->Write(*pkt)) { break; }
            // Reset the packet for the next batch using the arena.
            arena.Reset();
            pkt = google::protobuf::Arena::CreateMessage<telemetry::Packet>(&arena);
        }
    }
    sqlite3_finalize(stmt);

    // Flush any remaining samples.
    if (pkt->samples_size() > 0 && !ctx->IsCancelled()) {
        pkt->set_sequence(seq);
        writer->Write(*pkt);
    }

    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        spdlog::warn("FetchHistorical: sqlite3_step returned {}", rc);
    }

    return grpc::Status::OK;
}
