#include "client_app.h"
#include "../config/client_config.h"
#include "../common/logging.h"
#include <string>
#include <cstring>
#include <cstdlib>

static void print_usage(const char* prog) {
    std::printf(
        "Usage: %s [options]\n"
        "Options:\n"
        "  --server <host:port>   gRPC server address (default: localhost:50051)\n"
        "  --channels <n>         Number of channels to subscribe to (default: 4)\n"
        "  --width <px>           Window width in pixels  (default: 1280)\n"
        "  --height <px>          Window height in pixels (default: 800)\n"
        "  --fps <n>              Target frame rate        (default: 60)\n"
        "  --help                 Show this message\n",
        prog);
}

int main(int argc, char* argv[]) {
    init_client_logging("icu_monitor");
    spdlog::info("[main] ICU Telemetry Monitor starting");

    ClientConfig config; // defaults from client_config.h

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
        if (arg == "--server" && i + 1 < argc) {
            config.server_address = argv[++i];
        } else if (arg == "--channels" && i + 1 < argc) {
            const int v = std::atoi(argv[++i]);
            if (v > 0 && v <= 12) {
                config.channel_count = static_cast<uint32_t>(v);
            } else {
                std::fprintf(stderr, "Invalid --channels value (must be 1..12)\n");
                return 1;
            }
        } else if (arg == "--width" && i + 1 < argc) {
            config.window_width = std::atoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            config.window_height = std::atoi(argv[++i]);
        } else if (arg == "--fps" && i + 1 < argc) {
            config.target_fps = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
            print_usage(argv[0]);
            return 1;
        }
    }

    spdlog::info("[main] Config: server={} channels={} {}x{} @{}fps",
                 config.server_address, config.channel_count,
                 config.window_width, config.window_height, config.target_fps);

    ClientApp app(config);
    app.run();

    spdlog::info("[main] Exiting cleanly");
    return 0;
}
