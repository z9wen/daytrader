#include "daytrader/analysis/LeveragedExecutionAnalyzer.hpp"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] daytrader::domain::LongOpportunity ready_signal()
{
    return daytrader::domain::LongOpportunity{
        .bullish_score = 90,
        .phase = daytrader::domain::BullishPhase::strong,
        .entry = daytrader::domain::LongEntryDecision::ready,
        .if_held = daytrader::domain::HoldingGuidance::hold,
    };
}

[[nodiscard]] daytrader::domain::EntryZone zone(
    daytrader::domain::EntryZoneState state
)
{
    return daytrader::domain::EntryZone{
        .symbol = "SOXL",
        .lower_price = 49.90,
        .upper_price = 50.10,
        .current_price = 50.00,
        .state = state,
    };
}

[[nodiscard]] daytrader::domain::OrderFlowAssessment flow(
    daytrader::domain::OrderFlowPressureState pressure,
    double quality = 80.0
)
{
    return daytrader::domain::OrderFlowAssessment{
        .evidence_quality_percent = quality,
        .pressure = pressure,
    };
}

void leveraged_price_state_controls_entry()
{
    using namespace daytrader;
    const analysis::LeveragedExecutionAnalyzer analyzer;

    const auto extended = analyzer.analyze(
        ready_signal(),
        zone(domain::EntryZoneState::extended),
        std::nullopt,
        nullptr,
        false
    );
    require(extended.entry == domain::LongEntryDecision::wait_for_vwap,
            "an extended leveraged ETF must wait for its own VWAP zone");

    const auto in_zone = analyzer.analyze(
        ready_signal(),
        zone(domain::EntryZoneState::in_zone),
        std::nullopt,
        nullptr,
        false
    );
    require(in_zone.entry == domain::LongEntryDecision::ready,
            "an in-zone leveraged ETF should be ready when flow is optional");

    auto extended_signal = ready_signal();
    extended_signal.entry = domain::LongEntryDecision::wait_for_vwap;
    const auto leveraged_timing = analyzer.analyze(
        extended_signal,
        zone(domain::EntryZoneState::in_zone),
        std::nullopt,
        nullptr,
        false
    );
    require(leveraged_timing.entry == domain::LongEntryDecision::ready,
            "a strong signal ETF must delegate VWAP timing to the leveraged ETF");
}

void signal_etf_order_flow_confirms_entry()
{
    using namespace daytrader;
    const analysis::LeveragedExecutionAnalyzer analyzer;
    const auto entry_zone = zone(domain::EntryZoneState::in_zone);

    const auto missing = analyzer.analyze(
        ready_signal(), entry_zone, std::nullopt, nullptr, true
    );
    require(missing.entry == domain::LongEntryDecision::wait_for_flow,
            "missing QQQ/SOXX flow must not produce READY");

    const auto low_quality = analyzer.analyze(
        ready_signal(),
        entry_zone,
        flow(domain::OrderFlowPressureState::buying_effective, 49.0),
        nullptr,
        true
    );
    require(low_quality.entry == domain::LongEntryDecision::wait_for_flow,
            "low-quality DeltaRatio must remain WAIT_FLOW");

    const auto buying = analyzer.analyze(
        ready_signal(),
        entry_zone,
        flow(domain::OrderFlowPressureState::buying_effective),
        nullptr,
        true
    );
    require(buying.entry == domain::LongEntryDecision::ready,
            "effective buying with sufficient quality should confirm READY");

    const auto absorbed = analyzer.analyze(
        ready_signal(),
        entry_zone,
        flow(domain::OrderFlowPressureState::buying_absorbed),
        nullptr,
        true
    );
    require(absorbed.entry == domain::LongEntryDecision::wait_for_flow,
            "absorbed buying should wait instead of chasing trapped demand");

    const auto selling = analyzer.analyze(
        ready_signal(),
        entry_zone,
        flow(domain::OrderFlowPressureState::selling_effective),
        nullptr,
        true
    );
    require(selling.entry == domain::LongEntryDecision::avoid,
            "effective selling should reject a new leveraged long");
}

[[nodiscard]] daytrader::domain::PositionSnapshot position(
    double peak,
    double current,
    double giveback_percent
)
{
    return daytrader::domain::PositionSnapshot{
        .symbol = "SOXL",
        .quantity = 100.0,
        .average_cost = 50.0,
        .unrealized_pnl = current,
        .peak_unrealized_pnl = peak,
        .giveback_amount = peak - current,
        .giveback_percent = giveback_percent,
    };
}

void mfe_giveback_tightens_holding_guidance()
{
    using namespace daytrader;
    const analysis::LeveragedExecutionAnalyzer analyzer;
    const auto entry_zone = zone(domain::EntryZoneState::in_zone);

    auto held = position(500.0, 375.0, 25.0);
    auto decision = analyzer.analyze(
        ready_signal(), entry_zone, std::nullopt, &held, false
    );
    require(decision.if_held == domain::HoldingGuidance::protect,
            "20% or more profit giveback should protect");

    held = position(500.0, 300.0, 40.0);
    decision = analyzer.analyze(
        ready_signal(), entry_zone, std::nullopt, &held, false
    );
    require(decision.if_held == domain::HoldingGuidance::trim,
            "35% or more profit giveback should trim");

    held = position(500.0, 200.0, 60.0);
    decision = analyzer.analyze(
        ready_signal(), entry_zone, std::nullopt, &held, false
    );
    require(decision.if_held == domain::HoldingGuidance::exit,
            "50% or more profit giveback should exit");

    held = position(10.0, 0.0, 100.0);
    decision = analyzer.analyze(
        ready_signal(), entry_zone, std::nullopt, &held, false
    );
    require(decision.if_held == domain::HoldingGuidance::hold,
            "tiny peak profit should not activate noisy giveback protection");
}

void base_signal_is_never_softened()
{
    using namespace daytrader;
    auto weak = ready_signal();
    weak.entry = domain::LongEntryDecision::avoid;
    weak.if_held = domain::HoldingGuidance::exit;
    const auto decision = analysis::LeveragedExecutionAnalyzer{}.analyze(
        weak,
        zone(domain::EntryZoneState::in_zone),
        flow(domain::OrderFlowPressureState::buying_effective),
        nullptr,
        true
    );
    require(decision.entry == domain::LongEntryDecision::avoid,
            "leveraged price and flow must not override a weak signal ETF");
    require(decision.if_held == domain::HoldingGuidance::exit,
            "profit protection must never soften an existing EXIT");
}

void qqq_flow_drives_tqqq_execution()
{
    using namespace daytrader;
    const auto decision = analysis::LeveragedExecutionAnalyzer{}.analyze_market(
        domain::EtfSnapshot{
            .symbol = "QQQ",
            .trend_signal = domain::MarketTrendSignal::strong,
        },
        zone(domain::EntryZoneState::in_zone),
        flow(domain::OrderFlowPressureState::buying_effective),
        nullptr
    );
    require(decision.entry == domain::LongEntryDecision::ready,
            "QQQ BUY_EFFECTIVE should confirm an in-zone TQQQ entry");
}

} // namespace

int main()
{
    try {
        leveraged_price_state_controls_entry();
        signal_etf_order_flow_confirms_entry();
        mfe_giveback_tightens_holding_guidance();
        base_signal_is_never_softened();
        qqq_flow_drives_tqqq_execution();
        std::cout << "LeveragedExecutionTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "LeveragedExecutionTests failed: " << exception.what() << '\n';
        return 1;
    }
}
