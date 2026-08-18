#pragma once

#include "daytrader/backtest/BacktestReport.hpp"
#include "daytrader/domain/InstrumentBars.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace daytrader::backtest {

// Fixed parameters for the first falsifiable SOXX -> SOXL strategy. New signals
// are limited to the empirically strongest morning window and always execute on
// the next bar, never on the bar that produced the decision.
struct DayTradeBacktestSettings {
    std::string strategy_name;
    std::string signal_symbol{"SOXX"};
    std::string trade_symbol{"SOXL"};
    std::string time_zone{"America/New_York"};
    int entry_start_minute{9 * 60 + 45};
    int entry_end_minute{11 * 60 + 30};
    int forced_exit_signal_minute{15 * 60 + 45};
    double initial_stop_atr{1.0};
    double trailing_activation_atr{0.75};
    double trailing_distance_atr{0.75};
    double per_side_cost_basis_points{2.0};
    bool require_leveraged_vwap_zone{};
    // Older bars may still be supplied as indicator warm-up, but they cannot
    // create trades or inflate the reported number of tested sessions.
    std::optional<std::int64_t> earliest_entry_timestamp;
};

class DayTradeBacktester {
public:
    explicit DayTradeBacktester(DayTradeBacktestSettings settings);

    [[nodiscard]] BacktestReport run(
        const std::vector<domain::InstrumentBars>& instruments
    ) const;

private:
    DayTradeBacktestSettings settings_;
};

} // namespace daytrader::backtest
