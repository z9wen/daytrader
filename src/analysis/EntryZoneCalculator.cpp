#include "daytrader/analysis/EntryZoneCalculator.hpp"

#include "daytrader/analysis/AnalysisParameters.hpp"
#include "daytrader/analysis/EtfSnapshotCalculator.hpp"
#include "daytrader/indicators/TechnicalIndicators.hpp"

#include <algorithm>
#include <utility>

namespace daytrader::analysis {

std::optional<domain::EntryZone> EntryZoneCalculator::calculate(
    std::string symbol,
    std::span<const domain::MarketBar* const> completed_bars,
    const time::TimeZoneFormatter& time_formatter
) const
{
    if (completed_bars.size() < minimum_analysis_bars) {
        return std::nullopt;
    }

    const auto snapshot = EtfSnapshotCalculator{}.calculate(
        symbol,
        completed_bars,
        time_formatter
    );
    if (!snapshot.session_vwap.has_value()) {
        return std::nullopt;
    }

    const double atr = indicators::average_true_range(completed_bars, atr_period);
    if (atr <= 0.0) {
        return std::nullopt;
    }
    const double fast_atr = indicators::average_true_range(
        completed_bars,
        fast_atr_period
    );

    const double half_width = atr * entry_zone_atr_half_width;
    const double lower = *snapshot.session_vwap - half_width;
    const double upper = *snapshot.session_vwap + half_width;
    const auto state = snapshot.close > upper
        ? domain::EntryZoneState::extended
        : (snapshot.close < lower
            ? domain::EntryZoneState::below_zone
            : domain::EntryZoneState::in_zone);

    return domain::EntryZone{
        .symbol = std::move(symbol),
        .lower_price = std::max(0.0, lower),
        .upper_price = upper,
        .current_price = snapshot.close,
        .extended_vwap = snapshot.extended_vwap,
        .regular_vwap = snapshot.regular_vwap,
        .session_vwap = *snapshot.session_vwap,
        .atr14 = atr,
        .atr5 = fast_atr,
        .atr_percent = snapshot.close > 0.0 ? atr / snapshot.close * 100.0 : 0.0,
        .atr_expansion_ratio = fast_atr / atr,
        .state = state,
    };
}

} // namespace daytrader::analysis
