#include "daytrader/analysis/LiveTradeContextEnricher.hpp"

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

domain::LiveTradeContext LiveTradeContextEnricher::enrich(
    const domain::MarketScan& scan,
    domain::LiveTradeContext context
) const
{
    const OrderFlowSignalAnalyzer analyzer;
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
    return context;
}

} // namespace daytrader::analysis
