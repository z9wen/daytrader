#pragma once

#include "daytrader/backtest/BacktestReport.hpp"

#include <span>
#include <string>

namespace daytrader::presentation {

class BacktestReportPrinter {
public:
    explicit BacktestReportPrinter(std::string time_zone);

    [[nodiscard]] std::string render(
        std::span<const backtest::BacktestReport> reports
    ) const;

private:
    std::string time_zone_;
};

} // namespace daytrader::presentation
