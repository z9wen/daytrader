#include "daytrader/analysis/LeveragedExecutionAnalyzer.hpp"

#include "daytrader/analysis/AnalysisParameters.hpp"

#include <algorithm>
#include <cmath>

namespace daytrader::analysis {
namespace {

[[nodiscard]] domain::LongEntryDecision execution_entry(
    domain::LongEntryDecision signal_entry,
    const std::optional<domain::EntryZone>& zone,
    const std::optional<domain::OrderFlowAssessment>& flow,
    bool require_flow
)
{
    if (signal_entry != domain::LongEntryDecision::ready
        && signal_entry != domain::LongEntryDecision::watch) {
        return signal_entry;
    }
    if (!zone.has_value()) {
        return domain::LongEntryDecision::avoid;
    }
    switch (zone->state) {
    case domain::EntryZoneState::extended:
        return domain::LongEntryDecision::wait_for_vwap;
    case domain::EntryZoneState::below_zone:
    case domain::EntryZoneState::trend_unconfirmed:
        return domain::LongEntryDecision::avoid;
    case domain::EntryZoneState::in_zone:
        break;
    }
    if (!require_flow) {
        return domain::LongEntryDecision::ready;
    }
    if (!flow.has_value()) {
        return domain::LongEntryDecision::wait_for_flow;
    }
    switch (flow->pressure) {
    case domain::OrderFlowPressureState::buying_effective:
    case domain::OrderFlowPressureState::selling_absorbed:
        return domain::LongEntryDecision::ready;
    case domain::OrderFlowPressureState::selling_effective:
        return domain::LongEntryDecision::avoid;
    case domain::OrderFlowPressureState::insufficient_data:
    case domain::OrderFlowPressureState::buying_absorbed:
        return domain::LongEntryDecision::wait_for_flow;
    case domain::OrderFlowPressureState::balanced:
        return !flow->directional_score.has_value()
                || *flow->directional_score >= 0.0
            ? domain::LongEntryDecision::ready
            : domain::LongEntryDecision::wait_for_flow;
    }
    return domain::LongEntryDecision::wait_for_flow;
}

[[nodiscard]] domain::LongEntryDecision signal_gate_for_execution(
    const domain::LongOpportunity& signal
)
{
    // A strong signal ETF that is merely extended from its own VWAP still
    // supplies valid direction. Entry timing belongs to the leveraged ETF's
    // independent zone, so do not carry the signal ETF's WAIT_VWAP forward.
    if (signal.phase == domain::BullishPhase::strong
        && signal.entry == domain::LongEntryDecision::wait_for_vwap) {
        return domain::LongEntryDecision::ready;
    }
    return signal.entry;
}

[[nodiscard]] int guidance_severity(domain::HoldingGuidance guidance)
{
    switch (guidance) {
    case domain::HoldingGuidance::hold:
        return 0;
    case domain::HoldingGuidance::protect:
        return 1;
    case domain::HoldingGuidance::trim:
        return 2;
    case domain::HoldingGuidance::exit:
        return 3;
    }
    return 1;
}

[[nodiscard]] domain::HoldingGuidance stricter_guidance(
    domain::HoldingGuidance signal,
    domain::HoldingGuidance profit_protection
)
{
    return guidance_severity(profit_protection) > guidance_severity(signal)
        ? profit_protection
        : signal;
}

[[nodiscard]] domain::HoldingGuidance protected_holding_guidance(
    domain::HoldingGuidance signal,
    const domain::PositionSnapshot* position
)
{
    if (position == nullptr || position->quantity <= 0.0
        || !position->peak_unrealized_pnl.has_value()
        || !position->giveback_percent.has_value()) {
        return signal;
    }
    const double cost_basis = std::abs(position->quantity * position->average_cost);
    if (cost_basis <= 0.0) {
        return signal;
    }
    const double peak_return_percent = *position->peak_unrealized_pnl
        / cost_basis * 100.0;
    if (peak_return_percent < profit_protection_minimum_peak_return_percent) {
        return signal;
    }
    if ((position->unrealized_pnl.has_value() && *position->unrealized_pnl <= 0.0)
        || *position->giveback_percent >= profit_exit_giveback_percent) {
        return stricter_guidance(signal, domain::HoldingGuidance::exit);
    }
    if (*position->giveback_percent >= profit_trim_giveback_percent) {
        return stricter_guidance(signal, domain::HoldingGuidance::trim);
    }
    if (*position->giveback_percent >= profit_protect_giveback_percent) {
        return stricter_guidance(signal, domain::HoldingGuidance::protect);
    }
    return signal;
}

[[nodiscard]] domain::LongOpportunity market_signal_opportunity(
    const domain::EtfSnapshot& signal
)
{
    switch (signal.trend_signal) {
    case domain::MarketTrendSignal::strong:
        return domain::LongOpportunity{
            .bullish_score = 80,
            .phase = domain::BullishPhase::strong,
            .entry = domain::LongEntryDecision::ready,
            .if_held = domain::HoldingGuidance::hold,
        };
    case domain::MarketTrendSignal::weak:
        return domain::LongOpportunity{
            .bullish_score = 0,
            .phase = domain::BullishPhase::weak,
            .entry = domain::LongEntryDecision::avoid,
            .if_held = domain::HoldingGuidance::exit,
        };
    case domain::MarketTrendSignal::neutral:
        return domain::LongOpportunity{
            .bullish_score = 50,
            .phase = domain::BullishPhase::building,
            .entry = domain::LongEntryDecision::watch,
            .if_held = domain::HoldingGuidance::protect,
        };
    }
    return {};
}

} // namespace

domain::LeveragedExecutionDecision LeveragedExecutionAnalyzer::analyze(
    const domain::LongOpportunity& signal,
    const std::optional<domain::EntryZone>& leveraged_entry_zone,
    const std::optional<domain::OrderFlowAssessment>& order_flow,
    const domain::PositionSnapshot* position,
    bool require_order_flow_confirmation
) const
{
    return domain::LeveragedExecutionDecision{
        .entry = execution_entry(
            signal_gate_for_execution(signal),
            leveraged_entry_zone,
            order_flow,
            require_order_flow_confirmation
        ),
        .if_held = protected_holding_guidance(signal.if_held, position),
    };
}

domain::LeveragedExecutionDecision LeveragedExecutionAnalyzer::analyze_market(
    const domain::EtfSnapshot& signal,
    const std::optional<domain::EntryZone>& leveraged_entry_zone,
    const std::optional<domain::OrderFlowAssessment>& order_flow,
    const domain::PositionSnapshot* position,
    bool require_order_flow_confirmation
) const
{
    return analyze(
        market_signal_opportunity(signal),
        leveraged_entry_zone,
        order_flow,
        position,
        require_order_flow_confirmation
    );
}

} // namespace daytrader::analysis
