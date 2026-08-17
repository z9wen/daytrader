#include "daytrader/app/Application.hpp"

#include "daytrader/backtest/IbkrBacktestRunner.hpp"
#include "daytrader/config/AppConfig.hpp"
#include "daytrader/monitoring/MarketMonitor.hpp"
#include "daytrader/presentation/BacktestReportPrinter.hpp"
#include "daytrader/runtime/ShutdownSignal.hpp"

#include <charconv>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

[[nodiscard]] int parse_backtest_days(std::string_view text)
{
    int value{};
    const auto [end, error] = std::from_chars(
        text.data(),
        text.data() + text.size(),
        value
    );
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument("backtest days must be an integer");
    }
    return value;
}

void print_usage()
{
    std::cout << "Usage:\n"
              << "  daytrader                 continuous live monitor\n"
              << "  daytrader backtest [days] fetch IBKR RTH data and backtest (default 240)\n";
}

} // namespace

namespace daytrader::app {

int Application::run(int argc, char* argv[]) const
{
    try {
        const auto config = config::AppConfig::from_environment();
        if (argc >= 2) {
            const std::string_view command{argv[1]};
            if (command == "--help" || command == "-h") {
                print_usage();
                return 0;
            }
            if (command != "backtest" || argc > 3) {
                print_usage();
                return 2;
            }
            const int days = argc == 3 ? parse_backtest_days(argv[2]) : 240;
            const auto reports = backtest::IbkrBacktestRunner{}.run(config, days);
            std::cout << presentation::BacktestReportPrinter{config.time_zone}.render(reports);
            return 0;
        }

        runtime::ShutdownSignal::install();
        monitoring::MarketMonitor{config}.run([] {
            return runtime::ShutdownSignal::requested();
        });
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "daytrader: " << exception.what() << '\n';
        return 1;
    }
}

} // namespace daytrader::app
