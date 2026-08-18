#pragma once

#include "daytrader/config/MarketDataSettings.hpp"
#include "daytrader/universe/EtfDefinition.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace daytrader::config {

// Runtime cadence and retry policy for the continuous monitor.
struct MonitoringSettings {
    std::chrono::seconds bar_interval{std::chrono::minutes{5}};
    std::chrono::seconds reconnect_delay{5};
    // Fast live bootstrap; the scanner merges this with its durable bar cache.
    std::string history_duration{"2 D"};
    std::size_t history_maximum_bars{4'096};
    std::chrono::seconds initial_data_timeout{60};
    int live_context_client_id{8};
};

// Complete immutable configuration assembled once at application startup.
struct AppConfig {
    IbkrConnectionSettings ibkr;
    MonitoringSettings monitoring;
    std::vector<universe::EtfDefinition> etfs;
    std::string time_zone{"America/New_York"};
    std::filesystem::path data_directory{"data/ibkr/rth_5m"};

    // Applies DAYTRADER_* environment overrides and validates the result.
    [[nodiscard]] static AppConfig from_environment();
};

} // namespace daytrader::config
