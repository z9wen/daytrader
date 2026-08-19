#pragma once

#include "daytrader/backtest/BacktestReport.hpp"
#include "daytrader/config/AppConfig.hpp"

#include <span>
#include <string>
#include <vector>

namespace daytrader::backtest {

// Fetches complete one-minute batches from IBKR and evaluates two VWAP-entry variants
// over one shared dataset so their results are directly comparable.
class IbkrBacktestRunner {
public:
    // Downloads and durably merges only the requested symbols. This is kept
    // separate from run() so data acquisition can be resumed or diagnosed
    // without requiring every instrument needed by a strategy backtest.
    void cache_history(
        const config::AppConfig& config,
        int calendar_days,
        std::span<const std::string> symbols
    ) const;

    [[nodiscard]] std::vector<BacktestReport> run(
        const config::AppConfig& config,
        int calendar_days
    ) const;

    // Runs the QQQ -> TQQQ strategy strictly from durable local one-minute
    // files. Missing SPY/QQQ/TQQQ sessions are reported instead of triggering
    // a second IBKR request alongside an active cache process.
    [[nodiscard]] std::vector<BacktestReport> run_cached_qqq(
        const config::AppConfig& config,
        int calendar_days
    ) const;

    // Strictly local replay of SPY/QQQ/TQQQ/SOXX/SOXL. It never opens an
    // IBKR client, so it can run alongside a separate cache-history process.
    [[nodiscard]] std::vector<BacktestReport> run_cached_core(
        const config::AppConfig& config,
        int calendar_days
    ) const;
};

} // namespace daytrader::backtest
