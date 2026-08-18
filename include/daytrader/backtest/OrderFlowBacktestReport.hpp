#pragma once

#include "daytrader/backtest/BacktestReport.hpp"
#include "daytrader/domain/OrderFlow.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace daytrader::backtest {

enum class OrderFlowVerdict {
    confirmed,
    rejected,
    insufficient_data,
};

struct OrderFlowCandidate {
    TradeRecord trade;
    domain::OrderFlowWindow thirty_seconds;
    domain::OrderFlowWindow one_minute;
    domain::OrderFlowWindow five_minutes;
    domain::OrderFlowAssessment assessment;
    std::size_t raw_trades{};
    std::size_t raw_quotes{};
    int sampled_seconds{};
    OrderFlowVerdict verdict{OrderFlowVerdict::insufficient_data};
    std::string error;
};

struct OrderFlowSubsetStats {
    std::size_t candidates{};
    std::size_t wins{};
    double win_rate_percent{};
    double average_net_return_percent{};
};

struct OrderFlowBacktestReport {
    BacktestReport baseline;
    std::vector<OrderFlowCandidate> candidates;
    std::size_t cache_hits{};
    std::size_t downloaded{};
    std::size_t confirmed{};
    std::size_t rejected{};
    std::size_t insufficient{};
    std::size_t confirmed_wins{};
    double confirmed_win_rate_percent{};
    double confirmed_average_net_return_percent{};
    OrderFlowSubsetStats bullish_flow;
    OrderFlowSubsetStats bearish_flow;
    OrderFlowSubsetStats balanced_flow;
};

[[nodiscard]] constexpr const char* to_string(OrderFlowVerdict verdict)
{
    switch (verdict) {
    case OrderFlowVerdict::confirmed:
        return "CONFIRM";
    case OrderFlowVerdict::rejected:
        return "REJECT";
    case OrderFlowVerdict::insufficient_data:
        return "INSUFFICIENT";
    }
    return "UNKNOWN";
}

} // namespace daytrader::backtest
