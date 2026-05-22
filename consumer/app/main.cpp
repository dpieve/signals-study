#include "app.h"
#include "../common/logging.h"
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

// Global pointer used by the signal handler.
static App* g_app = nullptr;

static void signal_handler(int /*sig*/) noexcept {
    if (g_app) { g_app->shutdown(); }
}

int main(int argc, char* argv[]) {
    ConsumerConfig cfg;

    // Parse optional flags.
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--db" || arg == "-d") && i + 1 < argc) {
            cfg.db_path = argv[++i];
        } else if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            cfg.grpc_port = static_cast<uint16_t>(std::stoul(argv[++i]));
        } else if ((arg == "--channels" || arg == "-c") && i + 1 < argc) {
            cfg.channel_count = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if ((arg == "--sample-rate") && i + 1 < argc) {
            cfg.sample_rate = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if ((arg == "--log" || arg == "-l") && i + 1 < argc) {
            // log file path — init_logging is called in App constructor;
            // pass a second call with the file here.
            init_logging("consumer", argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout
                << "Usage: consumer [options]\n"
                << "  --db, -d <path>        SQLite database path (default: telemetry.db)\n"
                << "  --port, -p <port>      gRPC port (default: 50051)\n"
                << "  --channels, -c <n>     Number of channels (default: 4)\n"
                << "  --sample-rate <hz>     Sample rate in Hz (default: 44100)\n"
                << "  --log, -l <file>       Log file path\n";
            return EXIT_SUCCESS;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return EXIT_FAILURE;
        }
    }

    App app(cfg);
    g_app = &app;

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    app.run();

    g_app = nullptr;
    return EXIT_SUCCESS;
}
