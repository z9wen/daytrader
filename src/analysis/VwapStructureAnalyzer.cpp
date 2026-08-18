#include "daytrader/analysis/VwapStructureAnalyzer.hpp"

#include "daytrader/indicators/TechnicalIndicators.hpp"

#include <algorithm>
#include <cmath>

namespace daytrader::analysis {

domain::VwapStructureState VwapStructureAnalyzer::analyze(
    std::span<const domain::MarketBar* const> bars,
    const time::TimeZoneFormatter& time_formatter
) const
{
    if (bars.size() < 2) {
        return domain::VwapStructureState::unavailable;
    }
    const auto current_vwap = indicators::session_vwap(bars, time_formatter);
    if (!current_vwap.has_value()) {
        return domain::VwapStructureState::unavailable;
    }
    const double current_close = bars.back()->close;
    if (time_formatter.format_date(bars[bars.size() - 2]->epoch_seconds)
        != time_formatter.format_date(bars.back()->epoch_seconds)) {
        return current_close >= *current_vwap
            ? domain::VwapStructureState::above_flat
            : domain::VwapStructureState::below;
    }
    const auto previous_bars = bars.first(bars.size() - 1);
    const auto previous_vwap = indicators::session_vwap(previous_bars, time_formatter);
    if (!previous_vwap.has_value()) {
        return domain::VwapStructureState::unavailable;
    }

    const double previous_close = bars[bars.size() - 2]->close;
    if (previous_close <= *previous_vwap && current_close > *current_vwap) {
        return domain::VwapStructureState::reclaimed;
    }
    if (previous_close >= *previous_vwap && current_close < *current_vwap) {
        return domain::VwapStructureState::lost;
    }
    if (current_close < *current_vwap) {
        return domain::VwapStructureState::below;
    }

    const double slope_tolerance = std::max(1e-8, std::abs(*current_vwap) * 1e-6);
    return *current_vwap > *previous_vwap + slope_tolerance
        ? domain::VwapStructureState::above_rising
        : domain::VwapStructureState::above_flat;
}

} // namespace daytrader::analysis
