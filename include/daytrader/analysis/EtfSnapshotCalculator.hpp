#pragma once

#include "daytrader/domain/MarketBar.hpp"
#include "daytrader/domain/MarketScan.hpp"
#include "daytrader/time/TimeZoneFormatter.hpp"

#include <span>
#include <string>

namespace daytrader::analysis {

// Converts completed bars into the close/VWAP/EMA snapshot shared by all views.
class EtfSnapshotCalculator {
public:
    [[nodiscard]] domain::EtfSnapshot calculate(
        std::string symbol,
        std::span<const domain::MarketBar* const> bars,
        const time::TimeZoneFormatter& time_formatter
    ) const;
};

} // namespace daytrader::analysis
