#pragma once

#include "daytrader/domain/MarketScan.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace daytrader::backtest {

enum class ExitReason {
    protective_stop,
    trailing_stop,
    momentum_faded,
    weak_signal,
    profit_giveback,
    session_end,
    data_end,
};

struct TradeRecord {
    std::string session_date;
    domain::MarketRegime market_regime_at_entry{domain::MarketRegime::neutral};
    std::size_t trade_number_in_session{};
    std::int64_t entry_timestamp{};
    std::int64_t exit_timestamp{};
    double signal_entry_price{};
    double entry_price{};
    double exit_price{};
    double signal_atr_at_entry{};
    double trade_atr_at_entry{};
    double signal_atr_percent_at_entry{};
    double trade_atr_percent_at_entry{};
    double signal_atr_expansion_ratio{};
    double trade_atr_expansion_ratio{};
    double gross_return_percent{};
    double net_return_percent{};
    double maximum_favorable_excursion_percent{};
    double maximum_adverse_excursion_percent{};
    ExitReason exit_reason{ExitReason::data_end};
};

struct BacktestReport {
    std::string strategy_name;
    std::string signal_symbol;
    std::string trade_symbol;
    std::string first_session;
    std::string last_session;
    std::size_t sessions{};
    std::size_t traded_sessions{};
    std::size_t maximum_trades_in_session{};
    std::size_t trades{};
    std::size_t wins{};
    std::size_t losses{};
    double win_rate_percent{};
    double average_net_return_percent{};
    double average_win_percent{};
    double average_loss_percent{};
    double compounded_return_percent{};
    double profit_factor{};
    double maximum_drawdown_percent{};
    double average_holding_minutes{};
    double average_mfe_percent{};
    std::vector<TradeRecord> trade_log;
};

[[nodiscard]] constexpr const char* to_string(ExitReason reason)
{
    switch (reason) {
    case ExitReason::protective_stop:
        return "PROTECTIVE_STOP";
    case ExitReason::trailing_stop:
        return "TRAILING_STOP";
    case ExitReason::momentum_faded:
        return "MOMENTUM_FADED";
    case ExitReason::weak_signal:
        return "WEAK_SIGNAL";
    case ExitReason::profit_giveback:
        return "PROFIT_GIVEBACK";
    case ExitReason::session_end:
        return "SESSION_END";
    case ExitReason::data_end:
        return "DATA_END";
    }
    return "UNKNOWN";
}

} // namespace daytrader::backtest
