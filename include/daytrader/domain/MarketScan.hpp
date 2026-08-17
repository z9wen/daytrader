#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace daytrader::domain {

// Broad direction derived from the synchronized SPY and QQQ trend states.
enum class MarketRegime {
    bullish,
    neutral,
    bearish,
};

// Absolute VWAP/EMA trend for one instrument.
enum class MarketTrendSignal {
    strong,
    neutral,
    weak,
};

// Relative performance classification versus the configured benchmark.
enum class RelativeStrengthSignal {
    strong,
    neutral,
    weak,
};

// Direction of VIX relative to its EMA and one-hour change.
enum class VolatilityTrend {
    rising,
    steady,
    falling,
};

// Directional reference only; no order execution exists in this project.
enum class TradeSide {
    long_side,
    short_side,
};

// Explains where current price sits relative to the VWAP/ATR reference band.
enum class EntryZoneState {
    in_zone,
    extended,
    below_zone,
    trend_unconfirmed,
};

// Instrument-specific reference band: session VWAP +/- 0.25 * ATR14.
struct EntryZone {
    std::string symbol;
    double lower_price{};
    double upper_price{};
    double current_price{};
    double session_vwap{};
    double atr14{};
    EntryZoneState state{EntryZoneState::trend_unconfirmed};
};

// Absolute trend inputs shown for broad-market and ranking rows.
struct EtfSnapshot {
    std::string symbol;
    double close{};
    std::optional<double> session_vwap;
    double ema20{};
    double ema20_change_percent{};
    MarketTrendSignal trend_signal{MarketTrendSignal::neutral};
};

// Optional VIX risk context; unavailable data never blocks ETF scans.
struct VolatilitySnapshot {
    double close{};
    double ema20{};
    double change_60_min_percent{};
    VolatilityTrend trend{VolatilityTrend::steady};
};

// Complete sector/industry row, including base and long-leveraged entry zones.
struct RankedEtf {
    std::string symbol;
    std::string name;
    std::string group;
    std::string benchmark_symbol;
    std::string leveraged_long_symbol;
    std::string leveraged_short_symbol;
    std::size_t aligned_bar_count{};
    double close{};
    std::optional<double> session_vwap;
    double ema20{};
    double ema20_change_percent{};
    double relative_ratio{};
    double relative_ratio_ema20{};
    double relative_change_60_min_percent{};
    RelativeStrengthSignal signal{RelativeStrengthSignal::neutral};
    std::optional<EntryZone> entry_zone;
    std::optional<EntryZone> leveraged_entry_zone;
};

// A directional suggestion emitted by the selector, never an executable order.
struct TradeCandidate {
    std::string signal_symbol;
    std::string trade_symbol;
    TradeSide side{TradeSide::long_side};
};

// Immutable snapshot consumed by all dashboard tabs for one completed 5-minute bar.
struct MarketScan {
    std::int64_t epoch_seconds{};
    std::size_t aligned_market_bar_count{};
    EtfSnapshot spy;
    EtfSnapshot qqq;
    std::optional<VolatilitySnapshot> vix;
    MarketRegime market_regime{MarketRegime::neutral};
    std::vector<RankedEtf> sector_rankings;
    // Kept as "rankings" for compatibility; this collection contains industries.
    std::vector<RankedEtf> rankings;
    std::optional<TradeCandidate> sector_candidate;
    std::optional<TradeCandidate> candidate;
};

[[nodiscard]] constexpr std::string_view to_string(MarketRegime regime)
{
    switch (regime) {
    case MarketRegime::bullish:
        return "BULLISH";
    case MarketRegime::bearish:
        return "BEARISH";
    case MarketRegime::neutral:
        return "NEUTRAL";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view to_string(MarketTrendSignal signal)
{
    switch (signal) {
    case MarketTrendSignal::strong:
        return "STRONG";
    case MarketTrendSignal::weak:
        return "WEAK";
    case MarketTrendSignal::neutral:
        return "NEUTRAL";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view to_string(RelativeStrengthSignal signal)
{
    switch (signal) {
    case RelativeStrengthSignal::strong:
        return "STRONG";
    case RelativeStrengthSignal::weak:
        return "WEAK";
    case RelativeStrengthSignal::neutral:
        return "NEUTRAL";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view to_string(VolatilityTrend trend)
{
    switch (trend) {
    case VolatilityTrend::rising:
        return "RISING";
    case VolatilityTrend::falling:
        return "FALLING";
    case VolatilityTrend::steady:
        return "STEADY";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view to_string(TradeSide side)
{
    switch (side) {
    case TradeSide::long_side:
        return "LONG";
    case TradeSide::short_side:
        return "SHORT";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view to_string(EntryZoneState state)
{
    switch (state) {
    case EntryZoneState::in_zone:
        return "IN_ZONE";
    case EntryZoneState::extended:
        return "EXTENDED";
    case EntryZoneState::below_zone:
        return "BELOW_ZONE";
    case EntryZoneState::trend_unconfirmed:
        return "NO_TREND";
    }
    return "UNKNOWN";
}

} // namespace daytrader::domain
