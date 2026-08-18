#pragma once

#include "daytrader/backtest/OrderFlowBacktestReport.hpp"

#include <string>

namespace daytrader::presentation {

class OrderFlowBacktestPrinter {
public:
    explicit OrderFlowBacktestPrinter(std::string time_zone);

    [[nodiscard]] std::string render(
        const backtest::OrderFlowBacktestReport& report
    ) const;

private:
    std::string time_zone_;
};

} // namespace daytrader::presentation
