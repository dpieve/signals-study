#pragma once
#include <string>
#include <memory>
#include <vector>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

/// Initialise the default spdlog logger.
///
/// Creates a multi-sink logger named @p name.
/// A coloured console sink is always added.
/// When @p log_file is non-empty a basic rotating file sink is also added.
/// The log level can be overridden by the CONSUMER_LOG_LEVEL environment
/// variable (trace/debug/info/warn/error/critical).
inline void init_logging(const std::string& name,
                         const std::string& log_file = "") {
    std::vector<spdlog::sink_ptr> sinks;

    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::debug);
    sinks.push_back(console_sink);

    if (!log_file.empty()) {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            log_file, /*truncate=*/false);
        file_sink->set_level(spdlog::level::trace);
        sinks.push_back(file_sink);
    }

    auto logger = std::make_shared<spdlog::logger>(
        name, sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::warn);

    // Allow environment-variable override.
    if (const char* env_level = std::getenv("CONSUMER_LOG_LEVEL")) {
        logger->set_level(spdlog::level::from_str(env_level));
    }

    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
}
