#include "daytrader/analysis/EtfSnapshotCalculator.hpp"

#include "daytrader/analysis/AnalysisParameters.hpp"
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
    const auto session_vwap = indicators::session_vwap(bars, time_formatter);
    const double close = closes.back();

    return domain::EtfSnapshot{
        .symbol = std::move(symbol),
        .close = close,
        .session_vwap = session_vwap,
        .ema20 = ema.current,
        .ema20_change_percent = ema_change,
        .trend_signal = classify_trend(close, session_vwap, ema_change),
    };
}

} // namespace daytrader::analysis
