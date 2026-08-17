#pragma once

#include "daytrader/domain/MarketScan.hpp"

#include <optional>
#include <span>

namespace daytrader::strategy {

// Selects one directional reference from already sorted rankings.
// Short/inverse results remain display-only and are never market-data subscriptions.
class LeveragedEtfSelector {
public:
    [[nodiscard]] std::optional<domain::TradeCandidate> select(
        domain::MarketRegime regime,
        std::span<const domain::RankedEtf> rankings
    ) const;
};

} // namespace daytrader::strategy
