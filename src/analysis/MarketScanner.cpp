#include "daytrader/analysis/MarketScanner.hpp"

#include "daytrader/analysis/EntryZoneCalculator.hpp"
#include "daytrader/analysis/EtfSnapshotCalculator.hpp"
#include "daytrader/analysis/LongOpportunityAnalyzer.hpp"
#include "daytrader/analysis/MarketRegimeAnalyzer.hpp"
#include "daytrader/analysis/RelativeStrengthRanker.hpp"
#include "daytrader/analysis/RelativeStrengthAnalyzer.hpp"
#include "daytrader/analysis/VixAnalyzer.hpp"
#include "daytrader/market_data/BarSeriesAligner.hpp"
#include "daytrader/market_data/InstrumentBarsLookup.hpp"
#include "daytrader/strategy/LeveragedEtfSelector.hpp"
#include "daytrader/time/TimeZoneFormatter.hpp"

#include <algorithm>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace daytrader::analysis {
namespace {

[[nodiscard]] std::optional<std::vector<const domain::MarketBar*>> completed_bars_for(
    const std::string& symbol,
    const market_data::InstrumentBarsLookup& bars,
    const market_data::BarSeriesAligner& aligner,
    std::int64_t required_timestamp
)
{
    const auto* instrument_bars = bars.find(symbol);
    if (instrument_bars == nullptr) {
        return std::nullopt;
    }

    const auto aligned = aligner.align_completed(*instrument_bars, bars.at("SPY"));
    if (aligned.empty() || aligned.back().epoch_seconds != required_timestamp) {
        return std::nullopt;
    }

    std::vector<const domain::MarketBar*> completed_bars;
    completed_bars.reserve(aligned.size());
    for (const auto& pair : aligned) {
        completed_bars.push_back(pair.signal);
    }
    return completed_bars;
}

[[nodiscard]] std::optional<domain::EntryZone> calculate_entry_zone(
    const std::string& symbol,
    const market_data::InstrumentBarsLookup& bars,
    const market_data::BarSeriesAligner& aligner,
    std::int64_t required_timestamp,
    const time::TimeZoneFormatter& time_formatter
)
{
    const auto completed_bars = completed_bars_for(
        symbol,
        bars,
        aligner,
        required_timestamp
    );
    if (!completed_bars.has_value()) {
        return std::nullopt;
    }
    return EntryZoneCalculator{}.calculate(
        symbol,
        *completed_bars,
        time_formatter
    );
}

[[nodiscard]] std::optional<domain::EtfSnapshot> calculate_snapshot(
    const std::string& symbol,
    const market_data::InstrumentBarsLookup& bars,
    const market_data::BarSeriesAligner& aligner,
    std::int64_t required_timestamp,
    const time::TimeZoneFormatter& time_formatter
)
{
    const auto completed_bars = completed_bars_for(
        symbol,
        bars,
        aligner,
        required_timestamp
    );
    if (!completed_bars.has_value()) {
        return std::nullopt;
    }
    return EtfSnapshotCalculator{}.calculate(
        symbol,
        *completed_bars,
        time_formatter
    );
}

[[nodiscard]] std::vector<domain::RankedEtf> build_rankings(
    universe::EtfGroup target_group,
    const std::vector<universe::EtfDefinition>& etfs,
    const market_data::InstrumentBarsLookup& bars,
    const market_data::BarSeriesAligner& aligner,
    std::int64_t required_timestamp,
    const time::TimeZoneFormatter& time_formatter
)
{
    const RelativeStrengthRanker ranker;
    std::vector<domain::RankedEtf> rankings;
    rankings.reserve(etfs.size());
    for (const auto& etf : etfs) {
        if (etf.group != target_group || etf.benchmark_symbol.empty()) {
            continue;
        }

        const auto pairs = aligner.align_completed(
            bars.at(etf.market_data.symbol),
            bars.at(etf.benchmark_symbol)
        );
        auto rank = ranker.rank(etf, pairs, required_timestamp, time_formatter);
        if (!rank.has_value()) {
            continue;
        }

        const auto signal_vs_qqq = aligner.align_completed(
            bars.at(etf.market_data.symbol),
            bars.at("QQQ")
        );
        if (!signal_vs_qqq.empty()
            && signal_vs_qqq.back().epoch_seconds == required_timestamp) {
            rank->relative_strength_vs_qqq =
                RelativeStrengthAnalyzer{}.analyze(signal_vs_qqq);
        }

        rank->entry_zone = calculate_entry_zone(
            rank->symbol,
            bars,
            aligner,
            required_timestamp,
            time_formatter
        );
        const bool show_leveraged_zone = target_group == universe::EtfGroup::industry
            || rank->signal == domain::RelativeStrengthSignal::strong;
        if (show_leveraged_zone && !rank->leveraged_long_symbol.empty()) {
            rank->leveraged_entry_zone = calculate_entry_zone(
                rank->leveraged_long_symbol,
                bars,
                aligner,
                required_timestamp,
                time_formatter
            );
        }
        rankings.push_back(std::move(*rank));
    }

    std::ranges::stable_sort(
        rankings,
        std::greater{},
        &domain::RankedEtf::relative_change_60_min_percent
    );
    return rankings;
}

void add_long_opportunities(
    std::vector<domain::RankedEtf>& rankings,
    domain::MarketRegime market_regime
)
{
    const LongOpportunityAnalyzer analyzer;
    for (auto& rank : rankings) {
        rank.long_opportunity = analyzer.analyze(rank, market_regime);
    }
}

} // namespace

MarketScanner::MarketScanner(std::string time_zone, std::chrono::seconds bar_interval)
    : time_formatter_{std::move(time_zone)}
    , bar_interval_{bar_interval}
{
    if (bar_interval_ <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("bar interval must be positive");
    }
}

domain::MarketScan MarketScanner::scan(
    const std::vector<domain::InstrumentBars>& instruments,
    const std::vector<universe::EtfDefinition>& etfs
) const
{
    const market_data::InstrumentBarsLookup bars{instruments};
    const market_data::BarSeriesAligner aligner{bar_interval_};
    const auto market_pairs = aligner.align_completed(bars.at("QQQ"), bars.at("SPY"));
    auto market = MarketRegimeAnalyzer{}.analyze(market_pairs, time_formatter_);
    auto tqqq = calculate_snapshot(
        "TQQQ",
        bars,
        aligner,
        market.epoch_seconds,
        time_formatter_
    );
    auto tqqq_entry_zone = calculate_entry_zone(
        "TQQQ",
        bars,
        aligner,
        market.epoch_seconds,
        time_formatter_
    );
    std::optional<domain::VolatilitySnapshot> vix;
    if (const auto* vix_bars = bars.find("VIX"); vix_bars != nullptr) {
        const auto vix_pairs = aligner.align_completed(*vix_bars, bars.at("SPY"));
        vix = VixAnalyzer{}.analyze(vix_pairs, market.epoch_seconds);
    }

    auto sector_rankings = build_rankings(
        universe::EtfGroup::sector,
        etfs,
        bars,
        aligner,
        market.epoch_seconds,
        time_formatter_
    );
    auto industry_rankings = build_rankings(
        universe::EtfGroup::industry,
        etfs,
        bars,
        aligner,
        market.epoch_seconds,
        time_formatter_
    );
    add_long_opportunities(sector_rankings, market.regime);
    add_long_opportunities(industry_rankings, market.regime);
    const strategy::LeveragedEtfSelector selector;
    auto sector_candidate = selector.select(market.regime, sector_rankings);
    auto industry_candidate = selector.select(market.regime, industry_rankings);

    return domain::MarketScan{
        .epoch_seconds = market.epoch_seconds,
        .aligned_market_bar_count = market.aligned_bar_count,
        .spy = std::move(market.spy),
        .qqq = std::move(market.qqq),
        .tqqq = std::move(tqqq),
        .tqqq_entry_zone = std::move(tqqq_entry_zone),
        .vix = std::move(vix),
        .market_regime = market.regime,
        .sector_rankings = std::move(sector_rankings),
        .rankings = std::move(industry_rankings),
        .sector_candidate = std::move(sector_candidate),
        .candidate = std::move(industry_candidate),
    };
}

} // namespace daytrader::analysis
