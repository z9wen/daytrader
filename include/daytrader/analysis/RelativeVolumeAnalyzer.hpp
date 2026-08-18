#pragma once

#include "daytrader/domain/MarketBar.hpp"
#include "daytrader/domain/MarketScan.hpp"
#include "daytrader/time/TimeZoneFormatter.hpp"

#include <cstddef>
#include <span>

namespace daytrader::analysis {

struct RelativeVolumeSettings {
    std::size_t lookback_sessions{20};
    std::size_t minimum_baseline_sessions{3};
    std::size_t maximum_baseline_age_days{45};
    double light_threshold{0.80};
    double expanding_threshold{1.20};
};

// Compares volume only with prior sessions at the same time of day. This keeps
// the normal U-shaped intraday volume curve from becoming a false signal.
class RelativeVolumeAnalyzer {
public:
    explicit RelativeVolumeAnalyzer(RelativeVolumeSettings settings = {});

    [[nodiscard]] domain::RelativeVolumeSnapshot analyze(
        std::span<const domain::MarketBar* const> bars,
        const time::TimeZoneFormatter& time_formatter
    ) const;

private:
    RelativeVolumeSettings settings_;
};

} // namespace daytrader::analysis
