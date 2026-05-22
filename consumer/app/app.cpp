#include "app.h"
#include "../grpc/telemetry_service.h"
#include "../common/logging.h"
#include "../common/thread_utils.h"
#include "../../generator/generator_simulations.h"
#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>
#include <cassert>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

App::App(ConsumerConfig cfg)
    : cfg_(std::move(cfg)),
      queue_(/*capacity=*/64 * 1024),
      ingestion_mgr_(queue_) {
    init_logging("consumer");
}

App::~App() {
    shutdown();
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------

void App::shutdown() noexcept {
    if (!running_.exchange(false)) { return; } // already stopped

    spdlog::info("App::shutdown: stopping generator...");
    generator_stop();

    spdlog::info("App::shutdown: waiting for threads...");
    // jthread destructors join automatically; we just stop them.
    persistence_thread_.request_stop();
    processing_thread_.request_stop();

    // gRPC server shutdown.
    if (grpc_server_) {
        grpc_server_->Shutdown(std::chrono::system_clock::now() + 2s);
        spdlog::info("App::shutdown: gRPC server stopped");
    }

    spdlog::info("App::shutdown: complete");
}

// ---------------------------------------------------------------------------
// Persistence loop
// ---------------------------------------------------------------------------

void App::persistence_loop(std::stop_token st) {
    set_thread_priority_realtime();

    auto writer_result = SqliteWriter::open(cfg_.db_path, cfg_.persistence);
    if (!writer_result) {
        spdlog::critical("persistence_loop: failed to open DB '{}': {}",
                         cfg_.db_path, writer_result.error().message);
        return;
    }
    SqliteWriter& writer = *writer_result;

    const auto batch_interval =
        std::chrono::milliseconds(cfg_.persistence.batch_interval_ms);

    std::vector<IngestedPacket> batch;
    batch.reserve(256);

    uint64_t commit_seq = 0;

    while (!st.stop_requested()) {
        batch.clear();

        // Collect packets for one batch interval.
        const auto deadline = std::chrono::steady_clock::now() + batch_interval;
        IngestedPacket pkt;
        while (std::chrono::steady_clock::now() < deadline) {
            if (queue_.wait_dequeue_timed(pkt, st)) {
                batch.push_back(std::move(pkt));
            } else if (st.stop_requested()) {
                break;
            }
        }

        if (batch.empty()) { continue; }

        spdlog::debug("persistence_loop: flushing {} packets", batch.size());

        auto flush_result = writer.flush_batch(std::span<const IngestedPacket>(batch));
        if (!flush_result) {
            spdlog::error("persistence_loop: flush_batch failed: {}",
                          flush_result.error().message);
            continue;
        }

        // Post commit event — processing engine must see committed data only.
        CommittedBatch committed;
        committed.packets        = batch;
        committed.commit_sequence = ++commit_seq;
        events_.emit(std::move(committed));
    }

    // Drain remaining items after stop.
    IngestedPacket pkt;
    while (queue_.wait_dequeue_timed(pkt, st)) {
        batch.push_back(std::move(pkt));
    }
    if (!batch.empty()) {
        spdlog::info("persistence_loop: draining {} final packets", batch.size());
        if (auto r = writer.flush_batch(std::span<const IngestedPacket>(batch)); !r) {
            spdlog::error("persistence_loop: final flush failed: {}",
                          r.error().message);
        } else {
            CommittedBatch committed;
            committed.packets         = batch;
            committed.commit_sequence = ++commit_seq;
            events_.emit(std::move(committed));
        }
    }

    spdlog::info("persistence_loop: exiting");
}

// ---------------------------------------------------------------------------
// Processing loop
// ---------------------------------------------------------------------------

void App::processing_loop(std::stop_token st) {
    CommittedBatch batch;
    while (events_.wait_dequeue(batch, st)) {
        auto result = engine_.process_batch(batch);
        if (!result) {
            spdlog::error("processing_loop: {}", result.error().message);
            continue;
        }

        spdlog::debug("processing_loop: {} results for commit_seq={}",
                      result->size(), batch.commit_sequence);

        // Broadcast the raw committed packets to gRPC subscribers.
        broadcaster_.broadcast(batch);
    }

    spdlog::info("processing_loop: exiting");
}

// ---------------------------------------------------------------------------
// run
// ---------------------------------------------------------------------------

void App::run() {
    running_.store(true);

    // Open a secondary read-only SQLite connection for gRPC historical queries.
    // (The writer has its own connection with WAL, so reads are non-blocking.)
    const int orc = sqlite3_open_v2(
        cfg_.db_path.c_str(), &shared_db_,
        SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
    if (orc != SQLITE_OK) {
        spdlog::warn("App::run: could not open read-only DB for gRPC "
                     "historical queries: {}", sqlite3_errmsg(shared_db_));
        // Not fatal — historical queries will fail gracefully.
    }

    // Start persistence thread.
    persistence_thread_ = std::jthread(
        [this](std::stop_token st) { persistence_loop(std::move(st)); });

    // Start processing thread.
    processing_thread_ = std::jthread(
        [this](std::stop_token st) { processing_loop(std::move(st)); });

    // Build gRPC server.
    telemetry_service_ = std::make_unique<TelemetryServiceImpl>(
        broadcaster_, shared_db_, cfg_);

    const std::string address = "0.0.0.0:" + std::to_string(cfg_.grpc_port);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.RegisterService(telemetry_service_.get());
    grpc_server_ = builder.BuildAndStart();
    spdlog::info("App::run: gRPC server listening on {}", address);

    // Configure and start generator.
    // Build default channel configs: cycle ECG, SpO2, BP, EEG by index.
    const uint32_t nch = (cfg_.channel_count > 0) ? cfg_.channel_count : 4u;
    static const struct {
        const char*           name;
        const char*           attachment;
        GeneratorSimulationFn fn;
    } kDefaultChannelSpec[] = {
        {"ECG Lead I",          "Left Leg Electrode",    generator_sim_ecg},
        {"SpO2",                "Finger Sensor",         generator_sim_spo2},
        {"Arterial BP",         "Radial Artery",         generator_sim_bp},
        {"EEG Temporal Left",   "Temp-L Electrode",      generator_sim_eeg},
        {"ECG Lead II",         "Right Arm Electrode",   generator_sim_ecg},
        {"Respiration",         "Chest Electrode",       generator_sim_noise},
        {"EEG Frontal",         "Frontal Electrode",     generator_sim_eeg},
        {"ECG Lead III",        "Left Arm Electrode",    generator_sim_ecg},
        {"EEG Occipital",       "Occipital Electrode",   generator_sim_eeg},
        {"SpO2 Right",          "Right Finger Sensor",   generator_sim_spo2},
        {"BP Venous",           "Central Venous",        generator_sim_bp},
        {"Flatline",            "Reference",             generator_sim_flatline},
    };

    std::vector<ChannelConfig> channel_configs(nch);
    for (uint32_t i = 0; i < nch; ++i) {
        const auto& spec       = kDefaultChannelSpec[i % 12];
        channel_configs[i].channel_id = i;
        std::strncpy(channel_configs[i].name,       spec.name,       63);
        std::strncpy(channel_configs[i].attachment, spec.attachment, 63);
        channel_configs[i].simulation_fn        = spec.fn;
        channel_configs[i].simulation_user_data = nullptr;
    }

    GeneratorConfig gen_cfg{};
    gen_cfg.sample_rate             = cfg_.sample_rate;
    gen_cfg.packets_per_second      = cfg_.packets_per_second;
    gen_cfg.channel_count           = nch;
    gen_cfg.channels                = channel_configs.data();
    gen_cfg.max_samples_per_packet  = MAX_SAMPLES_PER_PACKET;
    gen_cfg.noise_level             = 0.01;
    gen_cfg.jitter_probability      = 0.0;
    gen_cfg.packet_drop_probability = 0.0;

    const GeneratorError gerr = generator_start(
        &gen_cfg,
        IngestionManager::packet_callback,
        /*event_callback=*/nullptr,
        /*user_data=*/&ingestion_mgr_);
    if (gerr != GENERATOR_OK) {
        spdlog::error("App::run: generator_start failed ({})", static_cast<int>(gerr));
    } else {
        spdlog::info("App::run: generator started with {} channels", nch);
    }

    // Block until shutdown() is called.
    while (running_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(100ms);
    }

    // Ensure threads are joined.
    persistence_thread_.request_stop();
    processing_thread_.request_stop();

    if (shared_db_) {
        sqlite3_close(shared_db_);
        shared_db_ = nullptr;
    }
}
