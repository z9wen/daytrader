#include "daytrader/analysis/EtfSnapshotCalculator.hpp"

#include "daytrader/analysis/AnalysisParameters.hpp"
#include "daytrader/analysis/RelativeVolumeAnalyzer.hpp"
#include "daytrader/analysis/VwapStructureAnalyzer.hpp"
#include "daytrader/indicators/TechnicalIndicators.hpp"

#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace daytrader::analysis {
namespace {

[[nodiscard]] domain::MarketTrendSignal classify_trend(
    double close,
    const std::optional<double>& session_vwap,
    double ema_change_percent
)
{
    if (session_vwap.has_value() && close > *session_vwap && ema_change_percent > 0.0) {
        return domain::MarketTrendSignal::strong;
    }
    if (session_vwap.has_value() && close < *session_vwap && ema_change_percent < 0.0) {
        return domain::MarketTrendSignal::weak;
    }
    return domain::MarketTrendSignal::neutral;
}

} // namespace

domain::EtfSnapshot EtfSnapshotCalculator::calculate(
    std::string symbol,
    std::span<const domain::MarketBar* const> bars,
    const time::TimeZoneFormatter& time_formatter
) const
{
    if (bars.size() < 2) {
        throw std::invalid_argument("ETF snapshot requires at least two bars");
    }

    std::vector<double> closes;
    closes.reserve(bars.size());
    for (const auto* bar : bars) {
        closes.push_back(bar->close);
    }

    const auto ema = indicators::exponential_moving_average(closes, ema_period);
    const double ema_change = ema.previous == 0.0
        ? 0.0
        : ((ema.current / ema.previous) - 1.0) * 100.0;
    const auto extended_vwap = indicators::extended_session_vwap(bars, time_formatter);
    const auto regular_vwap = indicators::regular_session_vwap(bars, time_formatter);
    const auto session_vwap = indicators::session_vwap(bars, time_formatter);
    const double close = closes.back();
    const double atr14 = indicators::average_true_range(bars, atr_period);
    const double atr5 = indicators::average_true_range(bars, fast_atr_period);

    return domain::EtfSnapshot{
        .symbol = std::move(symbol),
        .close = close,
        .extended_vwap = extended_vwap,
        .regular_vwap = regular_vwap,
        .session_vwap = session_vwap,
        .ema20 = ema.current,
        .ema20_change_percent = ema_change,
        .atr14 = atr14,
        .atr_expansion_ratio = atr14 > 0.0 ? atr5 / atr14 : 1.0,
        .vwap_structure = VwapStructureAnalyzer{}.analyze(bars, time_formatter),
        .relative_volume = RelativeVolumeAnalyzer{}.analyze(bars, time_formatter),
        .trend_signal = classify_trend(close, session_vwap, ema_change),
    };
}

} // namespace daytrader::analysis
