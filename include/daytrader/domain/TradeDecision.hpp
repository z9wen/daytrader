#pragma once

#include <string_view>

namespace daytrader::domain {

// Intraday bullish momentum stage for the signal ETF. This describes the
// opportunity and remains independent from whether the user owns a position.
enum class BullishPhase {
    building,
    strong,
    neutral,
    fading,
    weak,
};

// Entry guidance for a flat account. No value in this enum submits an order.
enum class LongEntryDecision {
    avoid,
    watch,
    wait_for_vwap,
    ready,
};

// Guidance to apply only when the corresponding leveraged ETF is already held.
enum class HoldingGuidance {
    hold,
    protect,
    trim,
    exit,
};

// A transparent score and its two separate decisions: entering and managing a
// position. Keeping them separate prevents STRONG from becoming a chase signal.
struct LongOpportunity {
    int bullish_score{};
    BullishPhase phase{BullishPhase::neutral};
    LongEntryDecision entry{LongEntryDecision::avoid};
    HoldingGuidance if_held{HoldingGuidance::protect};
};

[[nodiscard]] constexpr std::string_view to_string(BullishPhase phase)
{
    switch (phase) {
    case BullishPhase::building:
        return "BUILDING";
    case BullishPhase::strong:
        return "STRONG";
    case BullishPhase::neutral:
        return "NEUTRAL";
    case BullishPhase::fading:
        return "FADING";
    case BullishPhase::weak:
        return "WEAK";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view to_string(LongEntryDecision decision)
{
    switch (decision) {
    case LongEntryDecision::avoid:
        return "AVOID";
    case LongEntryDecision::watch:
        return "WATCH";
    case LongEntryDecision::wait_for_vwap:
        return "WAIT_VWAP";
    case LongEntryDecision::ready:
        return "READY";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view to_string(HoldingGuidance guidance)
{
    switch (guidance) {
    case HoldingGuidance::hold:
        return "HOLD";
    case HoldingGuidance::protect:
        return "PROTECT";
    case HoldingGuidance::trim:
        return "TRIM";
    case HoldingGuidance::exit:
        return "EXIT";
    }
    return "UNKNOWN";
}

} // namespace daytrader::domain
