#pragma once

#include "daytrader/domain/MarketScan.hpp"
#include "daytrader/market_data/BarSeriesAligner.hpp"
#include "daytrader/time/TimeZoneFormatter.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace daytrader::analysis {

// Shared SPY/QQQ context calculated from identical completed timestamps.
struct MarketContext {
    std::int64_t epoch_seconds{};
    std::size_t aligned_bar_count{};
    domain::EtfSnapshot spy;
    domain::EtfSnapshot qqq;
    domain::MarketRegime regime{domain::MarketRegime::neutral};
};

// Determines the broad regime before sector or industry candidates are selected.
class MarketRegimeAnalyzer {
public:
    [[nodiscard]] MarketContext analyze(
        std::span<const market_data::AlignedBarPair> qqq_vs_spy,
        const time::TimeZoneFormatter& time_formatter
    ) const;
};

} // namespace daytrader::analysis
