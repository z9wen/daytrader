#pragma once

#include "daytrader/domain/LiveTradeContext.hpp"
#include "daytrader/domain/MarketScan.hpp"

#include <optional>

namespace daytrader::analysis {

// Combines a stable unleveraged signal with the leveraged instrument's own
// execution zone, optional live Order Flow, and optional open-position MFE.
class LeveragedExecutionAnalyzer {
public:
    [[nodiscard]] domain::LeveragedExecutionDecision analyze(
        const domain::LongOpportunity& signal,
        const std::optional<domain::EntryZone>& leveraged_entry_zone,
        const std::optional<domain::OrderFlowAssessment>& order_flow,
        const domain::PositionSnapshot* position,
        bool require_order_flow_confirmation
    ) const;

    // QQQ is the signal instrument for TQQQ and has no RankedEtf row, so its
    // absolute trend is first normalized into the same opportunity vocabulary.
    [[nodiscard]] domain::LeveragedExecutionDecision analyze_market(
        const domain::EtfSnapshot& signal,
        const std::optional<domain::EntryZone>& leveraged_entry_zone,
        const std::optional<domain::OrderFlowAssessment>& order_flow,
        const domain::PositionSnapshot* position
    ) const;
};

} // namespace daytrader::analysis
