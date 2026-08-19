#include "daytrader/app/Application.hpp"

#include "daytrader/backtest/IbkrBacktestRunner.hpp"
#include "daytrader/backtest/OrderFlowBacktestRunner.hpp"
#include "daytrader/config/AppConfig.hpp"
#include "daytrader/monitoring/MarketMonitor.hpp"
#include "daytrader/presentation/BacktestReportPrinter.hpp"
#include "daytrader/presentation/OrderFlowBacktestPrinter.hpp"
#include "daytrader/runtime/ShutdownSignal.hpp"
#include "daytrader/time/TimeZoneFormatter.hpp"
#include "daytrader/universe/EtfUniverse.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

[[nodiscard]] int ytd_calendar_days(std::string_view time_zone)
{
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    const auto date = daytrader::time::TimeZoneFormatter{std::string{time_zone}}
        .format_date(now);
    const int year = std::stoi(date.substr(0, 4));
    const unsigned month = static_cast<unsigned>(std::stoi(date.substr(5, 2)));
    const unsigned day_of_month = static_cast<unsigned>(std::stoi(date.substr(8, 2)));
    using namespace std::chrono;
    const sys_days today = year_month_day{std::chrono::year{year},
                                          std::chrono::month{month},
                                          std::chrono::day{day_of_month}};
    const sys_days first_day = year_month_day{std::chrono::year{year}, January, day{1}};
    return static_cast<int>((today - first_day).count()) + 1;
}

[[nodiscard]] int parse_backtest_days(
    std::string_view text,
    std::string_view time_zone
)
{
    if (text == "ytd" || text == "YTD") {
        return ytd_calendar_days(time_zone);
    }
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

[[nodiscard]] std::vector<std::string> all_cache_symbols(
    const daytrader::config::AppConfig& config
)
{
    const auto requests = daytrader::universe::cacheable_etf_requests(config.etfs);
    std::vector<std::string> symbols;
    symbols.reserve(requests.size());
    std::unordered_set<std::string> seen;
    const auto append = [&](std::string_view symbol) {
        const auto found = std::ranges::find(
            requests,
            symbol,
            &daytrader::config::HistoricalDataSettings::symbol
        );
        if (found != requests.end() && seen.insert(found->symbol).second) {
            symbols.push_back(found->symbol);
        }
    };

    // Finish the two primary signal ETFs and their execution instruments first.
    constexpr std::array priorities{"QQQ", "TQQQ", "SPY", "SOXX", "SOXL"};
    for (const auto* symbol : priorities) {
        append(symbol);
    }
    for (const auto& request : requests) {
        append(request.symbol);
    }
    return symbols;
}

void print_usage()
{
    std::cout << "Usage:\n"
              << "  daytrader                 continuous live monitor\n"
              << "  daytrader cache-history [days|ytd] [all|SYMBOL...] cache 1-minute data\n"
              << "  daytrader backtest [days|ytd] fetch complete IBKR 1-minute data (default 240)\n"
              << "  daytrader backtest-qqq [days|ytd] use only local QQQ/TQQQ/SPY data\n"
              << "  daytrader backtest-core [days|ytd] use only local core-five ETF data\n"
              << "  daytrader orderflow-backtest [days|ytd] test SOXX tick flow (default 30)\n";
}

} // namespace

namespace daytrader::app {

int Application::run(int argc, char* argv[]) const
{
    try {
        const auto config = config::AppConfig::from_environment();
        std::clog << "IBKR TWS API SDK " << DAYTRADER_IBKR_API_VERSION << '\n';
        if (argc >= 2) {
            const std::string_view command{argv[1]};
            if (command == "--help" || command == "-h") {
                print_usage();
                return 0;
            }
            if (command == "cache-history") {
                if (argc < 4) {
                    print_usage();
                    return 2;
                }
                const int days = parse_backtest_days(argv[2], config.time_zone);
                std::vector<std::string> symbols;
                if (argc == 4
                    && (std::string_view{argv[3]} == "all"
                        || std::string_view{argv[3]} == "ALL")) {
                    symbols = all_cache_symbols(config);
                    std::clog << "Caching all " << symbols.size()
                              << " monitored long-side ETFs\n";
                } else {
                    symbols.reserve(static_cast<std::size_t>(argc - 3));
                    std::unordered_set<std::string> seen;
                    for (int index = 3; index < argc; ++index) {
                        if (seen.insert(argv[index]).second) {
                            symbols.emplace_back(argv[index]);
                        }
                    }
                }
                backtest::IbkrBacktestRunner{}.cache_history(
                    config,
                    days,
                    symbols
                );
                return 0;
            }
            if (command == "backtest-qqq") {
                if (argc > 3) {
                    print_usage();
                    return 2;
                }
                const int days = argc == 3
                    ? parse_backtest_days(argv[2], config.time_zone)
                    : 240;
                const auto reports = backtest::IbkrBacktestRunner{}.run_cached_qqq(
                    config,
                    days
                );
                std::cout << presentation::BacktestReportPrinter{config.time_zone}.render(
                    reports
                );
                return 0;
            }
            if (command == "backtest-core") {
                if (argc > 3) {
                    print_usage();
                    return 2;
                }
                const int days = argc == 3
                    ? parse_backtest_days(argv[2], config.time_zone)
                    : 240;
                const auto reports = backtest::IbkrBacktestRunner{}.run_cached_core(
                    config,
                    days
                );
                std::cout << presentation::BacktestReportPrinter{config.time_zone}.render(
                    reports
                );
                return 0;
            }
            if ((command != "backtest" && command != "orderflow-backtest")
                || argc > 3) {
                print_usage();
                return 2;
            }
            const int default_days = command == "orderflow-backtest" ? 30 : 240;
            const int days = argc == 3
                ? parse_backtest_days(argv[2], config.time_zone)
                : default_days;
            if (command == "orderflow-backtest") {
                const auto report = backtest::OrderFlowBacktestRunner{}.run(config, days);
                std::cout << presentation::OrderFlowBacktestPrinter{config.time_zone}.render(
                    report
                );
                return 0;
            }
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
