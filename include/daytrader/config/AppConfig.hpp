#pragma once

#include "daytrader/config/MarketDataSettings.hpp"
#include "daytrader/universe/EtfDefinition.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace daytrader::config {

// Runtime cadence and retry policy for the continuous monitor.
struct MonitoringSettings {
    std::chrono::seconds bar_interval{std::chrono::minutes{5}};
    std::chrono::seconds reconnect_delay{5};
};

// Complete immutable configuration assembled once at application startup.
struct AppConfig {
    IbkrConnectionSettings ibkr;
    MonitoringSettings monitoring;
    std::vector<universe::EtfDefinition> etfs;
    std::string time_zone{"America/New_York"};

    // Applies DAYTRADER_* environment overrides and validates the result.
    [[nodiscard]] static AppConfig from_environment();
};

} // namespace daytrader::config
