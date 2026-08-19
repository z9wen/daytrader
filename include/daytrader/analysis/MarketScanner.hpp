#pragma once

#include "daytrader/domain/InstrumentBars.hpp"
#include "daytrader/domain/MarketScan.hpp"
#include "daytrader/time/TimeZoneFormatter.hpp"
#include "daytrader/universe/EtfDefinition.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace daytrader::analysis {

// Orchestrates market context, relative-strength rankings, entry zones, and candidates.
class MarketScanner {
public:
    explicit MarketScanner(
        std::string time_zone,
        std::chrono::seconds bar_interval = std::chrono::minutes{5}
    );

    [[nodiscard]] domain::MarketScan scan(
        const std::vector<domain::InstrumentBars>& instruments,
        const std::vector<universe::EtfDefinition>& etfs
    ) const;

    // Uses stable larger bars for trend/RS and a separate faster completed-bar
    // view for VWAP, RVOL and entry timing.
    [[nodiscard]] domain::MarketScan scan(
        const std::vector<domain::InstrumentBars>& trend_instruments,
        const std::vector<domain::InstrumentBars>& execution_instruments,
        const std::vector<universe::EtfDefinition>& etfs,
        std::chrono::seconds execution_bar_interval
    ) const;

private:
    time::TimeZoneFormatter time_formatter_;
    std::chrono::seconds trend_bar_interval_;
};

} // namespace daytrader::analysis
