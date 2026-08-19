#pragma once

#include "daytrader/backtest/BacktestReport.hpp"
#include "daytrader/domain/InstrumentBars.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace daytrader::backtest {

enum class TradeLifecycleMode {
    // Existing responsive behavior: every fresh READY transition may enter,
    // and short-lived neutral readings can tighten or close the position.
    responsive,
    // One entry per BUILDING -> STRONG cycle. Hold ordinary neutral pullbacks;
    // exit only on confirmed five-minute deterioration or a clear weak trend.
    trend_cycle,
};

// Parameters for one signal-ETF -> leveraged-ETF intraday strategy. Historical
// bars cannot reproduce live Order Flow, so this engine evaluates the bar-based
// setup and the leveraged ETF's own VWAP/ATR execution state.
struct DayTradeBacktestSettings {
    std::string strategy_name;
    std::string signal_symbol{"SOXX"};
    std::string trade_symbol{"SOXL"};
    std::string time_zone{"America/New_York"};
    std::chrono::seconds source_bar_interval{std::chrono::minutes{5}};
    std::chrono::seconds trend_bar_interval{std::chrono::minutes{5}};
    TradeLifecycleMode lifecycle_mode{TradeLifecycleMode::responsive};
    // Optional research filters. By default every bar supplied by IBKR,
    // including premarket and after-hours, may produce an entry. Positions are
    // still closed when the New York trading date changes.
    std::optional<int> entry_start_minute;
    std::optional<int> entry_end_minute;
    std::optional<int> forced_exit_signal_minute;
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
