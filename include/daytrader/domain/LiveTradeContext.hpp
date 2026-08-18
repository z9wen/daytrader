#pragma once

#include "daytrader/domain/OrderFlow.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace daytrader::domain {

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

struct LiveTradeContext {
    std::int64_t updated_epoch_seconds{};
    bool positions_ready{};
    bool order_flow_connected{};
    std::vector<PositionSnapshot> positions;
    std::vector<LiveOrderFlowSnapshot> order_flow;
};

} // namespace daytrader::domain
