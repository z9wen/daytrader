#include "daytrader/analysis/LongOpportunityAnalyzer.hpp"

#include <algorithm>

namespace daytrader::analysis {
namespace {

[[nodiscard]] int bullish_score(
    const domain::RankedEtf& rank,
    domain::MarketRegime market_regime
)
{
    int score{};
    if (rank.session_vwap.has_value() && rank.close > *rank.session_vwap) {
        score += 20;
    }
    if (rank.ema20_change_percent > 0.0) {
        score += 20;
    }
    if (rank.relative_ratio > rank.relative_ratio_ema20) {
        score += 20;
    }
    if (rank.relative_change_60_min_percent > 0.0) {
        score += 20;
    }
    if (market_regime == domain::MarketRegime::bullish) {
        score += 20;
    } else if (market_regime == domain::MarketRegime::neutral) {
        score += 10;
    }
    return std::clamp(score, 0, 100);
}

[[nodiscard]] domain::BullishPhase classify_phase(
    const domain::RankedEtf& rank,
    int score
)
{
    // Broad-market direction is already reflected in bullish_score. It must
    // not override an independently strong industry: intraday leadership can
    // appear before SPY and QQQ recover. EXIT remains an instrument-level call.
    if (rank.signal == domain::RelativeStrengthSignal::weak) {
        return domain::BullishPhase::weak;
    }
    if (rank.signal == domain::RelativeStrengthSignal::strong && score >= 80) {
        return domain::BullishPhase::strong;
    }

    const bool above_vwap = rank.session_vwap.has_value()
        && rank.close > *rank.session_vwap;
    const bool positive_momentum = rank.ema20_change_percent > 0.0
        || rank.relative_change_60_min_percent > 0.0;
    if (score >= 60 && positive_momentum) {
        return domain::BullishPhase::building;
    }
    if (above_vwap && !positive_momentum) {
        return domain::BullishPhase::fading;
    }
    return domain::BullishPhase::neutral;
}

[[nodiscard]] domain::LongEntryDecision entry_decision(
    domain::BullishPhase phase,
    const std::optional<domain::EntryZone>& zone
)
{
    if (phase == domain::BullishPhase::building) {
        return domain::LongEntryDecision::watch;
    }
    if (phase != domain::BullishPhase::strong || !zone.has_value()) {
        return domain::LongEntryDecision::avoid;
    }

    switch (zone->state) {
    case domain::EntryZoneState::in_zone:
        return domain::LongEntryDecision::ready;
    case domain::EntryZoneState::extended:
        return domain::LongEntryDecision::wait_for_vwap;
    case domain::EntryZoneState::below_zone:
    case domain::EntryZoneState::trend_unconfirmed:
        return domain::LongEntryDecision::avoid;
    }
    return domain::LongEntryDecision::avoid;
}

[[nodiscard]] domain::HoldingGuidance holding_guidance(domain::BullishPhase phase)
{
    switch (phase) {
    case domain::BullishPhase::strong:
        return domain::HoldingGuidance::hold;
    case domain::BullishPhase::building:
    case domain::BullishPhase::neutral:
        return domain::HoldingGuidance::protect;
    case domain::BullishPhase::fading:
        return domain::HoldingGuidance::trim;
    case domain::BullishPhase::weak:
        return domain::HoldingGuidance::exit;
    }
    return domain::HoldingGuidance::protect;
}

} // namespace

domain::LongOpportunity LongOpportunityAnalyzer::analyze(
    const domain::RankedEtf& rank,
    domain::MarketRegime market_regime
) const
{
    const int score = bullish_score(rank, market_regime);
    const auto phase = classify_phase(rank, score);
    return domain::LongOpportunity{
        .bullish_score = score,
        .phase = phase,
        .entry = entry_decision(phase, rank.entry_zone),
        .if_held = holding_guidance(phase),
    };
}

} // namespace daytrader::analysis
