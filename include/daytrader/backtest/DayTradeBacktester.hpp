#pragma once

#include "daytrader/backtest/BacktestReport.hpp"
#include "daytrader/domain/InstrumentBars.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace daytrader::backtest {

// Parameters for one signal-ETF -> leveraged-ETF intraday strategy. Historical
// bars cannot reproduce live Order Flow, so this engine evaluates the bar-based
// setup and the leveraged ETF's own VWAP/ATR execution state.
struct DayTradeBacktestSettings {
    std::string strategy_name;
    std::string signal_symbol{"SOXX"};
    std::string trade_symbol{"SOXL"};
    std::string time_zone{"America/New_York"};
    // Scan the full practical RTH entry window. Premarket needs a separate
    // extended-hours cache and is deliberately not mixed into this dataset.
    int entry_start_minute{9 * 60 + 30};
    int entry_end_minute{15 * 60 + 30};
    int forced_exit_signal_minute{15 * 60 + 45};
    double initial_stop_atr{1.0};
    double trailing_activation_atr{0.75};
    double trailing_distance_atr{0.75};
    double per_side_cost_basis_points{2.0};
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
