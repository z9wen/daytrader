#pragma once

#include "daytrader/domain/MarketScan.hpp"
#include "daytrader/market_data/BarSeriesAligner.hpp"
#include "daytrader/time/TimeZoneFormatter.hpp"
#include "daytrader/universe/EtfDefinition.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace daytrader::analysis {

// Measures an ETF against its benchmark over the configured 60-minute lookback.
class RelativeStrengthRanker {
public:
    [[nodiscard]] std::optional<domain::RankedEtf> rank(
        const universe::EtfDefinition& etf,
        std::span<const market_data::AlignedBarPair> signal_vs_benchmark,
        std::int64_t required_timestamp,
        const time::TimeZoneFormatter& time_formatter
    ) const;
};

} // namespace daytrader::analysis
