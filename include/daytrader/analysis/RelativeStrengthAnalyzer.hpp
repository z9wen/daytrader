#pragma once

#include "daytrader/domain/MarketScan.hpp"
#include "daytrader/market_data/BarSeriesAligner.hpp"

#include <span>

namespace daytrader::analysis {

// Calculates relative-ratio change over three intraday horizons. Missing
// history produces an empty horizon instead of manufacturing a zero signal.
class RelativeStrengthAnalyzer {
public:
    [[nodiscard]] domain::RelativeStrengthHorizons analyze(
        std::span<const market_data::AlignedBarPair> signal_vs_benchmark
    ) const;
};

} // namespace daytrader::analysis
