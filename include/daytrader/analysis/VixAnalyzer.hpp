#pragma once

#include "daytrader/domain/MarketScan.hpp"
#include "daytrader/market_data/BarSeriesAligner.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace daytrader::analysis {

// Summarizes VIX direction as risk context; it does not alter trade candidates.
class VixAnalyzer {
public:
    [[nodiscard]] std::optional<domain::VolatilitySnapshot> analyze(
        std::span<const market_data::AlignedBarPair> vix_vs_spy,
        std::int64_t required_timestamp
    ) const;
};

} // namespace daytrader::analysis
