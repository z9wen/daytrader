#include "daytrader/app/Application.hpp"

#include "daytrader/backtest/IbkrBacktestRunner.hpp"
#include "daytrader/backtest/OrderFlowBacktestRunner.hpp"
#include "daytrader/config/AppConfig.hpp"
#include "daytrader/monitoring/MarketMonitor.hpp"
#include "daytrader/presentation/BacktestReportPrinter.hpp"
#include "daytrader/presentation/OrderFlowBacktestPrinter.hpp"
#include "daytrader/runtime/ShutdownSignal.hpp"
#include "daytrader/storage/MarketDataCsvStore.hpp"
#include "daytrader/time/TimeZoneFormatter.hpp"
#include "daytrader/universe/EtfUniverse.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <exception>
#include <iostream>
#include <optional>
#include <ranges>
#include <string>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

[[nodiscard]] std::chrono::sys_days current_market_day(std::string_view time_zone)
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
    return sys_days{year_month_day{std::chrono::year{year},
                                   std::chrono::month{month},
                                   std::chrono::day{day_of_month}}};
}

[[nodiscard]] int ytd_calendar_days(std::string_view time_zone)
{
    using namespace std::chrono;
    const auto today = current_market_day(time_zone);
    const year_month_day date{today};
    const sys_days first_day = date.year() / January / day{1};
    return static_cast<int>((today - first_day).count()) + 1;
}

[[nodiscard]] std::optional<std::chrono::sys_days> parse_iso_market_day(
    std::string_view text
)
{
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        return std::nullopt;
    }
    const auto parse_part = [&](std::size_t begin, std::size_t length)
        -> std::optional<int> {
        int value{};
        const auto first = text.data() + static_cast<std::ptrdiff_t>(begin);
        const auto last = first + static_cast<std::ptrdiff_t>(length);
        const auto [end, error] = std::from_chars(first, last, value);
        if (error != std::errc{} || end != last) {
            return std::nullopt;
        }
        return value;
    };
    const auto year_value = parse_part(0, 4);
    const auto month_value = parse_part(5, 2);
    const auto day_value = parse_part(8, 2);
    if (!year_value || !month_value || !day_value) {
        return std::nullopt;
    }
    const std::chrono::year_month_day date{
        std::chrono::year{*year_value},
        std::chrono::month{static_cast<unsigned>(*month_value)},
        std::chrono::day{static_cast<unsigned>(*day_value)},
    };
    return date.ok()
        ? std::optional<std::chrono::sys_days>{std::chrono::sys_days{date}}
        : std::nullopt;
}

[[nodiscard]] int parse_backtest_days(
    std::string_view text,
    std::string_view time_zone
)
{
    if (text == "ytd" || text == "YTD") {
        return ytd_calendar_days(time_zone);
    }
    if (const auto first_day = parse_iso_market_day(text); first_day.has_value()) {
        const auto today = current_market_day(time_zone);
        if (*first_day > today) {
            throw std::invalid_argument("historical start date cannot be in the future");
        }
        return static_cast<int>((today - *first_day).count()) + 1;
    }
    int value{};
    const auto [end, error] = std::from_chars(
        text.data(),
        text.data() + text.size(),
        value
    );
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument(
            "historical range must be days, ytd, or an ISO start date"
        );
    }
    return value;
}

[[nodiscard]] std::vector<std::string> signal_cache_symbols(
    const daytrader::config::AppConfig& config
)
{
    const auto requests = daytrader::universe::signal_cache_requests(config.etfs);
    std::vector<std::string> symbols;
    symbols.reserve(requests.size());
    for (const auto& request : requests) {
        symbols.push_back(request.symbol);
    }
    return symbols;
}

[[nodiscard]] std::vector<std::string> core_signal_cache_symbols()
{
    return {"QQQ", "SPY", "SOXX"};
}

void print_usage()
{
    std::cout << "Usage:\n"
              << "  daytrader                 continuous live monitor\n"
              << "  daytrader cache-history [days|ytd|YYYY-MM-DD] "
                 "[core|all|SYMBOL...] cache unleveraged 1-minute data\n"
              << "  daytrader migrate-cache       partition legacy CSVs by year\n"
              << "  daytrader backtest [days|ytd|YYYY-MM-DD] fetch complete IBKR 1-minute data (default 240)\n"
              << "  daytrader backtest-qqq [days|ytd|YYYY-MM-DD] use only local QQQ/TQQQ/SPY data\n"
              << "  daytrader backtest-core [days|ytd|YYYY-MM-DD] use only local core-five ETF data\n"
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
                    symbols = signal_cache_symbols(config);
                    std::clog << "Caching all " << symbols.size()
                              << " unleveraged signal ETFs; leveraged ETFs are excluded\n";
                } else if (argc == 4
                           && (std::string_view{argv[3]} == "core"
                               || std::string_view{argv[3]} == "CORE")) {
                    symbols = core_signal_cache_symbols();
                    std::clog << "Caching core signal ETFs: QQQ, SPY, SOXX\n";
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
            if (command == "migrate-cache") {
                if (argc != 2) {
                    print_usage();
                    return 2;
                }
                const storage::MarketDataCsvStore store{
                    config.minute_data_directory
                };
                const auto report = store.migrate_legacy_files();
                std::cout << "Migrated " << report.symbols << " symbols, "
                          << report.bars << " rows, "
                          << report.year_partitions << " year partitions\n"
                          << "Legacy backups: "
                          << report.archive_directory.string() << '\n';
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
