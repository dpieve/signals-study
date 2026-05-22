#pragma once
#include "../config/client_config.h"
#include "../ui/telemetry_view.h"
#include "../networking/grpc_client.h"
#include "../rendering/action_bar.h"
#include <vector>

/// Top-level application class for the ICU Telemetry Monitor.
///
/// Owns:
///   - Raylib window lifecycle (InitWindow / CloseWindow)
///   - The main render loop at the configured target FPS
///   - The gRPC client and its background stream thread
///   - The TelemetryView (all rendering state)
class ClientApp {
public:
    explicit ClientApp(ClientConfig cfg);
    ~ClientApp();

    // Non-copyable
    ClientApp(const ClientApp&)            = delete;
    ClientApp& operator=(const ClientApp&) = delete;

    /// Initialise the Raylib window, start the gRPC stream thread, and run
    /// the 60 FPS render loop until the window is closed or ESC is pressed.
    void run();

private:
    ClientConfig              config_;
    TelemetryView             view_;
    GrpcClient                grpc_client_;
};
