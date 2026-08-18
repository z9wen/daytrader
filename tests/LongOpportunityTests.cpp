#include "daytrader/analysis/LongOpportunityAnalyzer.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] daytrader::domain::RankedEtf bullish_rank(
    daytrader::domain::EntryZoneState zone_state
)
{
    return daytrader::domain::RankedEtf{
        .symbol = "SOXX",
        .close = 101.0,
        .session_vwap = 100.0,
        .ema20_change_percent = 0.05,
        .relative_ratio = 1.02,
        .relative_ratio_ema20 = 1.01,
        .relative_strength_vs_spy = daytrader::domain::RelativeStrengthHorizons{
            .fifteen_minute_percent = 0.2,
            .thirty_minute_percent = 0.3,
            .sixty_minute_percent = 0.4,
        },
        .relative_strength_vs_qqq = daytrader::domain::RelativeStrengthHorizons{
            .fifteen_minute_percent = 0.1,
            .thirty_minute_percent = 0.2,
            .sixty_minute_percent = 0.3,
        },
        .relative_change_60_min_percent = 0.4,
        .vwap_structure = daytrader::domain::VwapStructureState::above_rising,
        .relative_volume = daytrader::domain::RelativeVolumeSnapshot{
            .bar_ratio = 1.3,
            .state = daytrader::domain::RelativeVolumeState::expanding,
        },
        .signal = daytrader::domain::RelativeStrengthSignal::strong,
        .entry_zone = daytrader::domain::EntryZone{.state = zone_state},
    };
}

void strong_signal_separates_ready_from_chasing()
{
    using namespace daytrader;
    const analysis::LongOpportunityAnalyzer analyzer;

    const auto ready = analyzer.analyze(
        bullish_rank(domain::EntryZoneState::in_zone),
        domain::MarketRegime::bullish
    );
    require(ready.bullish_score == 100, "expected a transparent 100 bullish score");
    require(ready.phase == domain::BullishPhase::strong, "expected STRONG phase");
    require(ready.entry == domain::LongEntryDecision::ready, "VWAP zone should be READY");
    require(ready.if_held == domain::HoldingGuidance::hold, "strong position should HOLD");

    const auto extended = analyzer.analyze(
        bullish_rank(domain::EntryZoneState::extended),
        domain::MarketRegime::bullish
    );
    require(
        extended.entry == domain::LongEntryDecision::wait_for_vwap,
        "extended strong price must wait for VWAP instead of chasing"
    );
}

void weak_signal_exits_instead_of_opening()
{
    using namespace daytrader;
    auto rank = bullish_rank(domain::EntryZoneState::in_zone);
    rank.signal = domain::RelativeStrengthSignal::weak;

    const auto decision = analysis::LongOpportunityAnalyzer{}.analyze(
        rank,
        domain::MarketRegime::bullish
    );
    require(decision.phase == domain::BullishPhase::weak, "expected WEAK phase");
    require(decision.entry == domain::LongEntryDecision::avoid, "weak phase should AVOID");
    require(decision.if_held == domain::HoldingGuidance::exit, "weak phase should EXIT");
}

void strong_industry_remains_actionable_in_bearish_market()
{
    using namespace daytrader;
    const auto decision = analysis::LongOpportunityAnalyzer{}.analyze(
        bullish_rank(domain::EntryZoneState::in_zone),
        domain::MarketRegime::bearish
    );

    require(decision.bullish_score == 90,
            "bearish market should withhold only its 10-point context bonus");
    require(decision.phase == domain::BullishPhase::strong,
            "market direction must not override independent industry strength");
    require(decision.entry == domain::LongEntryDecision::ready,
            "independent strength at its VWAP zone should remain actionable");
    require(decision.if_held == domain::HoldingGuidance::hold,
            "an independently strong intraday position should remain HOLD");
}

void light_volume_requires_confirmation_before_entry()
{
    using namespace daytrader;
    auto rank = bullish_rank(domain::EntryZoneState::in_zone);
    rank.relative_volume.state = domain::RelativeVolumeState::light;
    const auto decision = analysis::LongOpportunityAnalyzer{}.analyze(
        rank,
        domain::MarketRegime::bullish
    );
    require(decision.phase == domain::BullishPhase::strong,
            "light RVOL should not erase an otherwise strong trend");
    require(decision.entry == domain::LongEntryDecision::watch,
            "light RVOL should require confirmation instead of READY");
}

void losing_vwap_trims_even_before_relative_strength_turns_weak()
{
    using namespace daytrader;
    auto rank = bullish_rank(domain::EntryZoneState::below_zone);
    rank.close = 99.0;
    rank.vwap_structure = domain::VwapStructureState::lost;
    const auto decision = analysis::LongOpportunityAnalyzer{}.analyze(
        rank,
        domain::MarketRegime::bullish
    );
    require(decision.phase == domain::BullishPhase::fading,
            "VWAP loss should mark the long structure FADING");
    require(decision.if_held == domain::HoldingGuidance::trim,
            "VWAP loss should advise TRIM before a full weak-signal EXIT");
}

} // namespace

int main()
{
    try {
        strong_signal_separates_ready_from_chasing();
        weak_signal_exits_instead_of_opening();
        strong_industry_remains_actionable_in_bearish_market();
        light_volume_requires_confirmation_before_entry();
        losing_vwap_trims_even_before_relative_strength_turns_weak();
        std::cout << "LongOpportunityTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "LongOpportunityTests failed: " << exception.what() << '\n';
        return 1;
    }
}
