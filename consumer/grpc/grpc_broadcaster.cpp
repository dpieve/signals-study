#include "grpc_broadcaster.h"
#include <algorithm>
#include <spdlog/spdlog.h>
#include <google/protobuf/arena.h>

void GrpcBroadcaster::add_subscriber(Subscriber s) {
    std::unique_lock lock(subscribers_mutex_);
    subscribers_.push_back(std::move(s));
    spdlog::info("GrpcBroadcaster: subscriber added (total={})",
                 subscribers_.size());
}

void GrpcBroadcaster::remove_subscriber(
        grpc::ServerWriter<telemetry::Packet>* writer) {
    std::unique_lock lock(subscribers_mutex_);
    const auto it = std::remove_if(
        subscribers_.begin(), subscribers_.end(),
        [writer](const Subscriber& s) { return s.writer == writer; });
    subscribers_.erase(it, subscribers_.end());
    spdlog::info("GrpcBroadcaster: subscriber removed (total={})",
                 subscribers_.size());
}

bool GrpcBroadcaster::broadcast(const CommittedBatch& batch) noexcept {
    if (batch.packets.empty()) { return false; }

    // Build one proto Packet per IngestedPacket using Arena allocation.
    // We do this once and reuse for all subscribers.
    std::vector<google::protobuf::Arena> arenas(batch.packets.size());
    std::vector<telemetry::Packet*>      proto_packets(batch.packets.size());

    for (std::size_t pi = 0; pi < batch.packets.size(); ++pi) {
        const auto& pkt = batch.packets[pi];
        auto* proto_pkt = google::protobuf::Arena::Create<telemetry::Packet>(
            &arenas[pi]);

        // Detect sequence-level gap.
        bool has_gap = (pkt.has_gap != 0);
        if (!first_packet_ && pkt.sequence != last_sequence_ + 1) {
            has_gap = true;
        }
        first_packet_  = false;
        last_sequence_ = pkt.sequence;

        proto_pkt->set_sequence(pkt.sequence);
        proto_pkt->set_has_gap(has_gap);

        const uint32_t count = std::min(pkt.sample_count, MAX_SAMPLES_PER_PACKET);
        for (uint32_t i = 0; i < count; ++i) {
            const auto& s  = pkt.samples[i];
            auto*       ps = proto_pkt->add_samples();
            ps->set_timestamp_ns(s.timestamp_ns);
            ps->set_channel_id(s.channel_id);
            ps->set_value(s.value);
        }
        proto_packets[pi] = proto_pkt;
    }

    // Iterate subscribers under shared lock.
    std::vector<grpc::ServerWriter<telemetry::Packet>*> failed_writers;
    bool any_sent = false;

    {
        std::shared_lock lock(subscribers_mutex_);
        for (auto& sub : subscribers_) {
            for (std::size_t pi = 0; pi < proto_packets.size(); ++pi) {
                const auto& pkt = batch.packets[pi];

                // Channel filter: if subscriber requested specific channels,
                // skip packets that contain no matching samples.
                bool has_matching = sub.channels.empty();
                if (!has_matching) {
                    const uint32_t count =
                        std::min(pkt.sample_count, MAX_SAMPLES_PER_PACKET);
                    for (uint32_t i = 0; i < count && !has_matching; ++i) {
                        for (const uint32_t ch : sub.channels) {
                            if (pkt.samples[i].channel_id == ch) {
                                has_matching = true;
                                break;
                            }
                        }
                    }
                }
                if (!has_matching) { continue; }

                if (!sub.writer->Write(*proto_packets[pi])) {
                    spdlog::warn("GrpcBroadcaster: Write() failed, "
                                 "marking subscriber for removal");
                    failed_writers.push_back(sub.writer);
                    break; // Skip remaining packets for this subscriber.
                }
                any_sent = true;
            }
        }
    }

    // Remove failed subscribers (upgrade to exclusive lock).
    for (auto* w : failed_writers) {
        remove_subscriber(w);
    }

    return any_sent;
}
