#include "daytrader/analysis/MarketRegimeAnalyzer.hpp"

#include "daytrader/analysis/AnalysisParameters.hpp"
#include "daytrader/analysis/EtfSnapshotCalculator.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace daytrader::analysis {
namespace {

[[nodiscard]] domain::MarketRegime classify(
    const domain::EtfSnapshot& spy,
    const domain::EtfSnapshot& qqq
)
{
    if (spy.trend_signal == domain::MarketTrendSignal::strong
        && qqq.trend_signal == domain::MarketTrendSignal::strong) {
        return domain::MarketRegime::bullish;
    }
    if (spy.trend_signal == domain::MarketTrendSignal::weak
        && qqq.trend_signal == domain::MarketTrendSignal::weak) {
        return domain::MarketRegime::bearish;
    }
    return domain::MarketRegime::neutral;
}

} // namespace

MarketContext MarketRegimeAnalyzer::analyze(
    std::span<const market_data::AlignedBarPair> qqq_vs_spy,
    const time::TimeZoneFormatter& time_formatter
) const
{
    if (qqq_vs_spy.size() < minimum_analysis_bars) {
        throw std::runtime_error(
            "market scan requires at least " + std::to_string(minimum_analysis_bars)
            + " aligned SPY/QQQ bars; received " + std::to_string(qqq_vs_spy.size())
        );
    }

    std::vector<const domain::MarketBar*> qqq_bars;
    std::vector<const domain::MarketBar*> spy_bars;
    qqq_bars.reserve(qqq_vs_spy.size());
    spy_bars.reserve(qqq_vs_spy.size());
    for (const auto& pair : qqq_vs_spy) {
        qqq_bars.push_back(pair.signal);
        spy_bars.push_back(pair.benchmark);
    }

    const EtfSnapshotCalculator snapshots;
    auto spy = snapshots.calculate("SPY", spy_bars, time_formatter);
    auto qqq = snapshots.calculate("QQQ", qqq_bars, time_formatter);
    const auto regime = classify(spy, qqq);

    return MarketContext{
        .epoch_seconds = qqq_vs_spy.back().epoch_seconds,
        .aligned_bar_count = qqq_vs_spy.size(),
        .spy = std::move(spy),
        .qqq = std::move(qqq),
        .regime = regime,
    };
}

} // namespace daytrader::analysis
