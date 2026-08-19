#pragma once

#include "daytrader/backtest/BacktestReport.hpp"
#include "daytrader/config/AppConfig.hpp"

#include <vector>

namespace daytrader::backtest {

// Fetches complete one-minute batches from IBKR and evaluates two VWAP-entry variants
// over one shared dataset so their results are directly comparable.
class IbkrBacktestRunner {
public:
    [[nodiscard]] std::vector<BacktestReport> run(
        const config::AppConfig& config,
        int calendar_days
    ) const;
};

} // namespace daytrader::backtest
