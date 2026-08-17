#pragma once

#include "daytrader/domain/MarketBar.hpp"
#include "daytrader/domain/MarketScan.hpp"
#include "daytrader/time/TimeZoneFormatter.hpp"

#include <optional>
#include <span>
#include <string>

namespace daytrader::analysis {

// Calculates a reference price band from the instrument's own VWAP and ATR.
// The result is analytical output only; this class never creates an order.
class EntryZoneCalculator {
public:
    [[nodiscard]] std::optional<domain::EntryZone> calculate(
        std::string symbol,
        std::span<const domain::MarketBar* const> completed_bars,
        const time::TimeZoneFormatter& time_formatter
    ) const;
};

} // namespace daytrader::analysis
