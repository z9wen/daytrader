#include "daytrader/analysis/VixAnalyzer.hpp"

#include "daytrader/analysis/AnalysisParameters.hpp"
#include "daytrader/indicators/TechnicalIndicators.hpp"

#include <vector>

namespace daytrader::analysis {

std::optional<domain::VolatilitySnapshot> VixAnalyzer::analyze(
    std::span<const market_data::AlignedBarPair> vix_vs_spy,
    std::int64_t required_timestamp
) const
{
    if (vix_vs_spy.size() < minimum_analysis_bars
        || vix_vs_spy.back().epoch_seconds != required_timestamp) {
        return std::nullopt;
    }

    std::vector<double> closes;
    closes.reserve(vix_vs_spy.size());
    for (const auto& pair : vix_vs_spy) {
        closes.push_back(pair.signal->close);
    }

    const auto ema = indicators::exponential_moving_average(closes, ema_period);
    const double close = closes.back();
    const double prior = closes[closes.size() - 1 - relative_strength_lookback];
    if (prior <= 0.0) {
        return std::nullopt;
    }
    const double change = ((close / prior) - 1.0) * 100.0;

    auto trend = domain::VolatilityTrend::steady;
    if (close > ema.current && change > 0.0) {
        trend = domain::VolatilityTrend::rising;
    } else if (close < ema.current && change < 0.0) {
        trend = domain::VolatilityTrend::falling;
    }

    return domain::VolatilitySnapshot{
        .close = close,
        .ema20 = ema.current,
        .change_60_min_percent = change,
        .trend = trend,
    };
}

} // namespace daytrader::analysis
