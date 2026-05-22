#pragma once
#include <string>
#include <cstdint>

struct ClientConfig {
    std::string server_address = "localhost:50051";
    uint32_t    channel_count  = 4;
    int         window_width   = 1280;
    int         window_height  = 800;
    int         target_fps     = 60;
};
