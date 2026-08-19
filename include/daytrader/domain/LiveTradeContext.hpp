#pragma once

#include "daytrader/domain/OrderFlow.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace daytrader::domain {

// Values are the current EWrapper.marketDataType codes documented by IBKR.
enum class MarketDataFeedType {
    unknown = 0,
    live = 1,
    frozen = 2,
    delayed = 3,
    delayed_frozen = 4,
};

[[nodiscard]] constexpr const char* to_string(MarketDataFeedType type)
{
    switch (type) {
    case MarketDataFeedType::live:
        return "LIVE";
    case MarketDataFeedType::frozen:
        return "FROZEN";
    case MarketDataFeedType::delayed:
        return "DELAYED";
    case MarketDataFeedType::delayed_frozen:
        return "DELAYED_FROZEN";
    case MarketDataFeedType::unknown:
        return "UNKNOWN";
    }
    return "UNKNOWN";
}

// One read-only IBKR position. Peak P&L is measured from the time this process
// first observes the current quantity/average-cost pair, not from order time.
struct PositionSnapshot {
    std::string account;
    std::string symbol;
    int contract_id{};
    double quantity{};
    double average_cost{};
    std::optional<double> market_price;
    std::optional<double> market_value;
    std::optional<double> daily_pnl;
    std::optional<double> unrealized_pnl;
    std::optional<double> peak_unrealized_pnl;
    std::optional<double> giveback_amount;
    std::optional<double> giveback_percent;
};

// Raw rolling windows come from live time-and-sales. The ATR-aware assessment
// is filled by the scanner so the socket client remains independent of strategy.
struct LiveOrderFlowSnapshot {
    std::string symbol;
    std::int64_t updated_epoch_seconds{};
    OrderFlowWindow thirty_seconds;
    OrderFlowWindow one_minute;
    std::optional<OrderFlowAssessment> assessment;
};

// One streaming Level-1 quote. selected_price prefers mark, then last, then
// midpoint/bid/ask and is kept separate from the last completed candle close.
struct LiveQuoteSnapshot {
    std::string symbol;
    std::int64_t updated_epoch_seconds{};
    MarketDataFeedType feed_type{MarketDataFeedType::unknown};
    std::optional<double> bid;
    std::optional<double> ask;
    std::optional<double> last;
    std::optional<double> mark;
    std::optional<double> selected_price;
};

struct LiveTradeContext {
    std::int64_t updated_epoch_seconds{};
    bool positions_ready{};
    bool order_flow_connected{};
    std::vector<PositionSnapshot> positions;
    std::vector<LiveOrderFlowSnapshot> order_flow;
    std::vector<LiveQuoteSnapshot> quotes;
};

} // namespace daytrader::domain
