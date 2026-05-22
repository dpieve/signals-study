#pragma once
#include <cstdint>
#include <string>

struct PersistenceConfig {
    int batch_interval_ms  = 100;
    int busy_timeout_ms    = 5000;
    int wal_autocheckpoint = 0;
    int page_size          = 8192;
    int cache_size_kb      = 65536;
};

struct ConsumerConfig {
    uint32_t    sample_rate         = 44100;
    uint32_t    packets_per_second  = 1000;
    uint32_t    channel_count       = 4;
    uint16_t    grpc_port           = 50051;
    std::string db_path             = "telemetry.db";
    PersistenceConfig persistence;
};
