#pragma once

#include "daytrader/domain/MarketBar.hpp"
#include "daytrader/domain/MarketScan.hpp"
#include "daytrader/time/TimeZoneFormatter.hpp"

#include <span>

namespace daytrader::analysis {

class VwapStructureAnalyzer {
public:
    [[nodiscard]] domain::VwapStructureState analyze(
        std::span<const domain::MarketBar* const> bars,
        const time::TimeZoneFormatter& time_formatter
    ) const;
};

} // namespace daytrader::analysis
