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
    std::chrono::seconds source_bar_interval{std::chrono::minutes{1}};
    std::chrono::seconds bar_interval{std::chrono::minutes{5}};
    std::chrono::seconds reconnect_delay{5};
    // Live refresh is merged into a durable one-minute cache. Missing RVOL
    // baseline history is backfilled separately instead of truncating startup.
    std::string history_duration{"2 D"};
    // Zero means January 1 through today; a positive value is an explicit
    // user-selected research range rather than an internal cap.
    int history_lookback_days{};
    int live_context_client_id{8};
    int backfill_client_id{9};
};

// Complete immutable configuration assembled once at application startup.
struct AppConfig {
    IbkrConnectionSettings ibkr;
    MonitoringSettings monitoring;
    std::vector<universe::EtfDefinition> etfs;
    std::string time_zone{"America/New_York"};
    std::filesystem::path minute_data_directory{"data/ibkr/all_1m"};

    // Applies DAYTRADER_* environment overrides and validates the result.
    [[nodiscard]] static AppConfig from_environment();
};

} // namespace daytrader::config
