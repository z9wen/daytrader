#include "daytrader/analysis/RelativeStrengthRanker.hpp"

#include "daytrader/analysis/AnalysisParameters.hpp"
#include "daytrader/analysis/EtfSnapshotCalculator.hpp"
#include "daytrader/analysis/RelativeStrengthAnalyzer.hpp"
#include "daytrader/indicators/TechnicalIndicators.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace daytrader::analysis {
namespace {

[[nodiscard]] std::string group_name(universe::EtfGroup group)
{
    switch (group) {
    case universe::EtfGroup::broad_market:
        return "MARKET";
    case universe::EtfGroup::sector:
        return "SECTOR";
    case universe::EtfGroup::industry:
        return "INDUSTRY";
    }
    return "UNKNOWN";
}

} // namespace

std::optional<domain::RankedEtf> RelativeStrengthRanker::rank(
    const universe::EtfDefinition& etf,
    std::span<const market_data::AlignedBarPair> signal_vs_benchmark,
    std::int64_t required_timestamp,
    const time::TimeZoneFormatter& time_formatter
) const
{
    if (signal_vs_benchmark.size() < minimum_analysis_bars
        || signal_vs_benchmark.back().epoch_seconds != required_timestamp) {
        return std::nullopt;
    }

    std::vector<double> ratios;
    std::vector<const domain::MarketBar*> signal_bars;
    ratios.reserve(signal_vs_benchmark.size());
    signal_bars.reserve(signal_vs_benchmark.size());
    for (const auto& pair : signal_vs_benchmark) {
        if (pair.benchmark->close <= 0.0) {
            throw std::runtime_error(etf.benchmark_symbol + " returned a non-positive close price");
        }
        ratios.push_back(pair.signal->close / pair.benchmark->close);
        signal_bars.push_back(pair.signal);
    }

    const auto ratio_ema = indicators::exponential_moving_average(ratios, ema_period);
    const double current_ratio = ratios.back();
    const auto horizons = RelativeStrengthAnalyzer{}.analyze(signal_vs_benchmark);
    const double relative_change = horizons.sixty_minute_percent.value_or(0.0);

    auto signal = domain::RelativeStrengthSignal::neutral;
    if (current_ratio > ratio_ema.current && relative_change > 0.0) {
        signal = domain::RelativeStrengthSignal::strong;
    } else if (current_ratio < ratio_ema.current && relative_change < 0.0) {
        signal = domain::RelativeStrengthSignal::weak;
    }

    const auto snapshot = EtfSnapshotCalculator{}.calculate(
        etf.market_data.symbol,
        signal_bars,
        time_formatter
    );

    return domain::RankedEtf{
        .symbol = etf.market_data.symbol,
        .name = etf.name,
        .group = group_name(etf.group),
        .benchmark_symbol = etf.benchmark_symbol,
        .leveraged_long_symbol = etf.leveraged_long_symbol,
        .leveraged_short_symbol = etf.leveraged_short_symbol,
        .aligned_bar_count = signal_vs_benchmark.size(),
        .close = snapshot.close,
        .extended_vwap = snapshot.extended_vwap,
        .regular_vwap = snapshot.regular_vwap,
        .session_vwap = snapshot.session_vwap,
        .ema20 = snapshot.ema20,
        .ema20_change_percent = snapshot.ema20_change_percent,
        .relative_ratio = current_ratio,
        .relative_ratio_ema20 = ratio_ema.current,
        .relative_strength_vs_spy = horizons,
        .relative_change_60_min_percent = relative_change,
        .vwap_structure = snapshot.vwap_structure,
        .relative_volume = snapshot.relative_volume,
        .signal = signal,
    };
}

} // namespace daytrader::analysis
