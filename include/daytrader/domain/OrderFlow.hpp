#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace daytrader::domain {

// Historical IBKR time-and-sales carries second-resolution timestamps. The
// sequence preserves the order within each returned stream for stable replay.
struct TradeTick {
    std::int64_t epoch_seconds{};
    std::size_t sequence{};
    double price{};
    double size{};
};

struct BidAskTick {
    std::int64_t epoch_seconds{};
    std::size_t sequence{};
    double bid_price{};
    double ask_price{};
    double bid_size{};
    double ask_size{};
};

enum class TradeSide {
    buy,
    sell,
    unknown,
};

enum class TradeClassificationMethod {
    quote_test,
    tick_rule,
    unknown,
};

struct ClassifiedTrade {
    TradeTick trade;
    TradeSide side{TradeSide::unknown};
    TradeClassificationMethod method{TradeClassificationMethod::unknown};
};

// One normalized interval of aggressive volume. DeltaRatio is deliberately
// based only on classified volume; coverage discloses how much total volume was
// actually usable so a large ratio with weak evidence is not over-trusted.
struct OrderFlowBar {
    std::int64_t epoch_seconds{};
    double buy_volume{};
    double sell_volume{};
    double unknown_volume{};
    double delta{};
    std::optional<double> delta_ratio_percent;
    double classification_coverage_percent{};
    double quote_test_coverage_percent{};
    std::optional<double> average_quote_imbalance_percent;
    std::optional<double> first_trade_price;
    std::optional<double> last_trade_price;
    std::optional<double> price_change_basis_points;
    // Price displacement per absolute DeltaRatio percentage point. A small
    // value under strong positive Delta is a useful absorption warning.
    std::optional<double> impact_efficiency;
    std::size_t trade_count{};
};

// Raw event cache used to replay classification rules without repeatedly
// consuming IBKR historical-tick requests.
struct OrderFlowTicks {
    std::string symbol;
    std::int64_t requested_end_timestamp{};
    std::vector<TradeTick> trades;
    std::vector<BidAskTick> quotes;
};

struct OrderFlowWindow {
    std::int64_t start_timestamp{};
    std::int64_t end_timestamp{};
    bool complete{};
    OrderFlowBar flow;
    std::size_t quote_count{};
};

enum class OrderFlowPressureState {
    insufficient_data,
    balanced,
    buying_effective,
    buying_absorbed,
    selling_effective,
    selling_absorbed,
};

enum class AtrVolatilityState {
    unavailable,
    compressed,
    normal,
    expanding,
    extreme,
};

// Combines event imbalance with the 5-minute signal ETF's ATR. This separates
// pressure (DeltaRatio) from response (how far price actually moved).
struct OrderFlowAssessment {
    std::optional<double> delta_acceleration_points;
    std::optional<double> thirty_second_price_atr;
    std::optional<double> one_minute_price_atr;
    // Signed ATR displacement expected at a hypothetical 100% one-sided Delta.
    std::optional<double> normalized_impact_atr;
    // Signed, quality-adjusted diagnostic in [-100, 100]. This is deliberately
    // not a probability: positive values favor buyers and negative values favor
    // sellers after combining 30s/60s Delta, acceleration, and ATR response.
    std::optional<double> directional_score;
    double evidence_quality_percent{};
    OrderFlowPressureState pressure{OrderFlowPressureState::insufficient_data};
    AtrVolatilityState volatility{AtrVolatilityState::unavailable};
};

[[nodiscard]] constexpr const char* to_string(OrderFlowPressureState state)
{
    switch (state) {
    case OrderFlowPressureState::insufficient_data:
        return "NO_DATA";
    case OrderFlowPressureState::balanced:
        return "BALANCED";
    case OrderFlowPressureState::buying_effective:
        return "BUY_EFFECTIVE";
    case OrderFlowPressureState::buying_absorbed:
        return "BUY_ABSORBED";
    case OrderFlowPressureState::selling_effective:
        return "SELL_EFFECTIVE";
    case OrderFlowPressureState::selling_absorbed:
        return "SELL_ABSORBED";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr const char* to_string(AtrVolatilityState state)
{
    switch (state) {
    case AtrVolatilityState::unavailable:
        return "NO_ATR";
    case AtrVolatilityState::compressed:
        return "COMPRESSED";
    case AtrVolatilityState::normal:
        return "NORMAL";
    case AtrVolatilityState::expanding:
        return "EXPANDING";
    case AtrVolatilityState::extreme:
        return "EXTREME";
    }
    return "UNKNOWN";
}

} // namespace daytrader::domain
