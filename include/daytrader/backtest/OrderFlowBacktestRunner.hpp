#pragma once

#include "daytrader/backtest/OrderFlowBacktestReport.hpp"
#include "daytrader/config/AppConfig.hpp"

namespace daytrader::backtest {

class OrderFlowBacktestRunner {
public:
    [[nodiscard]] OrderFlowBacktestReport run(
        const config::AppConfig& config,
        int calendar_days = 30
    ) const;
};

} // namespace daytrader::backtest
