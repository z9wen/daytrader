#include "daytrader/presentation/BacktestReportPrinter.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] bool contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

void explains_the_revised_backtest_scope()
{
    daytrader::backtest::BacktestReport report{
        .strategy_name = "SOXX -> SOXL",
        .signal_symbol = "SOXX",
        .trade_symbol = "SOXL",
        .trade_log = {
            daytrader::backtest::TradeRecord{
                .session_date = "2024-01-02",
                .market_regime_at_entry = daytrader::domain::MarketRegime::bearish,
                .entry_timestamp = 1'704'205'800,
                .exit_timestamp = 1'704'206'100,
                .entry_price = 50.0,
                .exit_price = 50.5,
                .net_return_percent = 0.96,
            },
        },
    };
    const std::vector reports{report};
    const auto rendered = daytrader::presentation::BacktestReportPrinter{
        "America/New_York"
    }.render(reports);

    require(contains(rendered, "No hard SPY/QQQ market gate"),
            "report must state that the broad market is context, not a gate");
    require(contains(rendered, "RTH entries 09:30-15:30 ET"),
            "report must disclose its complete RTH entry window");
    require(contains(rendered, "each new BUILDING -> STRONG cycle may trade"),
            "report must disclose the cycle-based re-entry rule");
    require(contains(rendered, "Order Flow is NOT replayed"),
            "report must not imply missing historical flow was confirmed");
    require(contains(rendered, "BEARISH 1 (100.0% win, 0.960% avg)"),
            "report should expose bearish-context outcomes separately");
    require(contains(rendered, "OPEN 1 (100.0% win, 0.960% avg)"),
            "report should expose entry-time outcomes separately");
}

} // namespace

int main()
{
    try {
        explains_the_revised_backtest_scope();
        std::cout << "BacktestReportPrinterTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "BacktestReportPrinterTests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
