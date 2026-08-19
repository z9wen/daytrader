#include "daytrader/analysis/LiveTradeContextEnricher.hpp"

#include "daytrader/analysis/LeveragedExecutionAnalyzer.hpp"
#include "daytrader/analysis/OrderFlowSignalAnalyzer.hpp"

#include <optional>
#include <ranges>
#include <utility>

namespace daytrader::analysis {
namespace {

struct AtrContext {
    double atr{};
    double expansion_ratio{};
};

[[nodiscard]] const domain::LiveOrderFlowSnapshot* flow_for(
    const domain::LiveTradeContext& context,
    const std::string& symbol
)
{
    const auto found = std::ranges::find(
        context.order_flow,
        symbol,
        &domain::LiveOrderFlowSnapshot::symbol
    );
    return found == context.order_flow.end() ? nullptr : &*found;
}

[[nodiscard]] const domain::PositionSnapshot* position_for(
    const domain::LiveTradeContext& context,
    const std::string& symbol
)
{
    const auto found = std::ranges::find(
        context.positions,
        symbol,
        &domain::PositionSnapshot::symbol
    );
    return found == context.positions.end() ? nullptr : &*found;
}

[[nodiscard]] std::optional<double> live_price_for(
    const domain::LiveTradeContext& context,
    const std::string& symbol
)
{
    const auto found = std::ranges::find(
        context.quotes,
        symbol,
        &domain::LiveQuoteSnapshot::symbol
    );
    return found == context.quotes.end() ? std::nullopt : found->selected_price;
}

void apply_live_price(
    std::optional<domain::EntryZone>& zone,
    const std::optional<double>& price
)
{
    if (!zone.has_value() || !price.has_value()) {
        return;
    }
    zone->current_price = *price;
    zone->state = *price > zone->upper_price
        ? domain::EntryZoneState::extended
        : (*price < zone->lower_price
            ? domain::EntryZoneState::below_zone
            : domain::EntryZoneState::in_zone);
}

[[nodiscard]] std::optional<AtrContext> atr_for(
    const domain::MarketScan& scan,
    const std::string& symbol
)
{
    if (symbol == scan.qqq.symbol && scan.qqq.atr14 > 0.0) {
        return AtrContext{
            .atr = scan.qqq.atr14,
            .expansion_ratio = scan.qqq.atr_expansion_ratio,
        };
    }
    const auto find_in = [&](const std::vector<domain::RankedEtf>& rankings)
        -> std::optional<AtrContext> {
        const auto found = std::ranges::find(rankings, symbol, &domain::RankedEtf::symbol);
        if (found == rankings.end() || !found->entry_zone.has_value()
            || found->entry_zone->atr14 <= 0.0) {
            return std::nullopt;
        }
        return AtrContext{
            .atr = found->entry_zone->atr14,
            .expansion_ratio = found->entry_zone->atr_expansion_ratio,
        };
    };
    if (auto sector = find_in(scan.sector_rankings); sector.has_value()) {
        return sector;
    }
    return find_in(scan.rankings);
}

} // namespace

domain::MarketScan LiveTradeContextEnricher::enrich(
    domain::MarketScan scan,
    domain::LiveTradeContext context
) const
{
    const OrderFlowSignalAnalyzer analyzer;
    scan.spy.live_price = live_price_for(context, scan.spy.symbol);
    scan.qqq.live_price = live_price_for(context, scan.qqq.symbol);
    if (scan.tqqq.has_value()) {
        scan.tqqq->live_price = live_price_for(context, scan.tqqq->symbol);
        apply_live_price(scan.tqqq_entry_zone, scan.tqqq->live_price);
    }
    for (auto& flow : context.order_flow) {
        const auto atr = atr_for(scan, flow.symbol);
        if (!atr.has_value()) {
            continue;
        }
        flow.assessment = analyzer.analyze(
            flow.thirty_seconds,
            flow.one_minute,
            atr->atr,
            atr->expansion_ratio
        );
    }

    const LeveragedExecutionAnalyzer execution_analyzer;
    const auto update_rankings = [&](std::vector<domain::RankedEtf>& rankings) {
        for (auto& rank : rankings) {
            rank.live_price = live_price_for(context, rank.symbol);
            apply_live_price(rank.entry_zone, rank.live_price);
            if (rank.leveraged_long_symbol.empty()) {
                continue;
            }
            apply_live_price(
                rank.leveraged_entry_zone,
                live_price_for(context, rank.leveraged_long_symbol)
            );
            const auto* flow = flow_for(context, rank.symbol);
            rank.leveraged_execution = execution_analyzer.analyze(
                rank.long_opportunity,
                rank.leveraged_entry_zone,
                flow == nullptr ? std::nullopt : flow->assessment,
                position_for(context, rank.leveraged_long_symbol),
                rank.symbol == "SOXX"
            );
        }
    };
    update_rankings(scan.sector_rankings);
    update_rankings(scan.rankings);

    if (scan.tqqq.has_value()) {
        const auto* qqq_flow = flow_for(context, "QQQ");
        scan.tqqq_execution = execution_analyzer.analyze_market(
            scan.qqq,
            scan.tqqq_entry_zone,
            qqq_flow == nullptr ? std::nullopt : qqq_flow->assessment,
            position_for(context, "TQQQ")
        );
    }
    scan.live_context = std::move(context);
    return scan;
}

} // namespace daytrader::analysis
