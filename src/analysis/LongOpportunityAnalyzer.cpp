#include "daytrader/analysis/LongOpportunityAnalyzer.hpp"

#include <algorithm>

namespace daytrader::analysis {
namespace {

[[nodiscard]] int horizon_points(
    const domain::RelativeStrengthHorizons& horizons,
    int full_points
)
{
    const std::optional<double> values[] = {
        horizons.fifteen_minute_percent,
        horizons.thirty_minute_percent,
        horizons.sixty_minute_percent,
    };
    int available{};
    int positive{};
    for (const auto& value : values) {
        if (!value.has_value()) {
            continue;
        }
        ++available;
        positive += *value > 0.0 ? 1 : 0;
    }
    if (available == 0) {
        return -1;
    }
    if (positive == available && available >= 2) {
        return full_points;
    }
    if (positive * 2 >= available) {
        return full_points / 2;
    }
    return 0;
}

[[nodiscard]] bool bullish_vwap_structure(const domain::RankedEtf& rank)
{
    switch (rank.vwap_structure) {
    case domain::VwapStructureState::reclaimed:
    case domain::VwapStructureState::above_flat:
    case domain::VwapStructureState::above_rising:
        return true;
    case domain::VwapStructureState::below:
    case domain::VwapStructureState::lost:
        return false;
    case domain::VwapStructureState::unavailable:
        return rank.session_vwap.has_value() && rank.close > *rank.session_vwap;
    }
    return false;
}

[[nodiscard]] int bullish_score(
    const domain::RankedEtf& rank,
    domain::MarketRegime market_regime
)
{
    int score{};
    if (bullish_vwap_structure(rank)) {
        score += 20;
    }
    if (rank.ema20_change_percent > 0.0) {
        score += 15;
    }
    if (rank.relative_ratio > rank.relative_ratio_ema20) {
        score += 15;
    }
    const int spy_points = horizon_points(rank.relative_strength_vs_spy, 20);
    if (spy_points >= 0) {
        score += spy_points;
    } else if (rank.relative_change_60_min_percent > 0.0) {
        score += 20;
    }
    score += std::max(0, horizon_points(rank.relative_strength_vs_qqq, 10));
    if (rank.relative_volume.state == domain::RelativeVolumeState::expanding) {
        score += 10;
    } else if (rank.relative_volume.state == domain::RelativeVolumeState::normal) {
        score += 5;
    }
    if (market_regime == domain::MarketRegime::bullish) {
        score += 10;
    } else if (market_regime == domain::MarketRegime::neutral) {
        score += 5;
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
    if (rank.vwap_structure == domain::VwapStructureState::lost) {
        return domain::BullishPhase::fading;
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
    const domain::RankedEtf& rank,
    domain::BullishPhase phase,
    const std::optional<domain::EntryZone>& zone
)
{
    if ((phase != domain::BullishPhase::strong
         && phase != domain::BullishPhase::building)
        || !zone.has_value()) {
        return domain::LongEntryDecision::avoid;
    }

    switch (zone->state) {
    case domain::EntryZoneState::in_zone:
        if (rank.relative_volume.state == domain::RelativeVolumeState::light) {
            return domain::LongEntryDecision::watch;
        }
        return domain::LongEntryDecision::ready;
    case domain::EntryZoneState::extended:
        return domain::LongEntryDecision::wait_for_vwap;
    case domain::EntryZoneState::below_zone:
        return phase == domain::BullishPhase::building
            ? domain::LongEntryDecision::watch
            : domain::LongEntryDecision::avoid;
    case domain::EntryZoneState::trend_unconfirmed:
        return domain::LongEntryDecision::watch;
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
        .entry = entry_decision(rank, phase, rank.entry_zone),
        .if_held = holding_guidance(phase),
    };
}

} // namespace daytrader::analysis
