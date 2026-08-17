#include "daytrader/strategy/LeveragedEtfSelector.hpp"

#include <algorithm>

namespace daytrader::strategy {

std::optional<domain::TradeCandidate> LeveragedEtfSelector::select(
    domain::MarketRegime regime,
    std::span<const domain::RankedEtf> rankings
) const
{
    if (regime == domain::MarketRegime::bullish) {
        const auto candidate = std::ranges::find_if(rankings, [](const auto& rank) {
            return rank.signal == domain::RelativeStrengthSignal::strong
                && !rank.leveraged_long_symbol.empty()
                && rank.session_vwap.has_value()
                && rank.close > *rank.session_vwap
                && rank.ema20_change_percent > 0.0;
        });
        if (candidate != rankings.end()) {
            return domain::TradeCandidate{
                .signal_symbol = candidate->symbol,
                .trade_symbol = candidate->leveraged_long_symbol,
                .side = domain::TradeSide::long_side,
            };
        }
    }

    if (regime == domain::MarketRegime::bearish) {
        const auto candidate = std::find_if(rankings.rbegin(), rankings.rend(), [](const auto& rank) {
            return rank.signal == domain::RelativeStrengthSignal::weak
                && !rank.leveraged_short_symbol.empty()
                && rank.session_vwap.has_value()
                && rank.close < *rank.session_vwap
                && rank.ema20_change_percent < 0.0;
        });
        if (candidate != rankings.rend()) {
            return domain::TradeCandidate{
                .signal_symbol = candidate->symbol,
                .trade_symbol = candidate->leveraged_short_symbol,
                .side = domain::TradeSide::short_side,
            };
        }
    }

    return std::nullopt;
}

} // namespace daytrader::strategy
