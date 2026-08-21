#include "daytrader/monitoring/MarketMonitor.hpp"

#include "daytrader/analysis/MarketScanner.hpp"
#include "daytrader/analysis/LiveTradeContextEnricher.hpp"
#include "daytrader/analysis/SetupCalibrationEngine.hpp"
#include "daytrader/ibkr/IbkrErrorClassifier.hpp"
#include "daytrader/ibkr/HistoricalRequestPlanner.hpp"
#include "daytrader/ibkr/TwsLiveContextClient.hpp"
#include "daytrader/ibkr/TwsMarketDataClient.hpp"
#include "daytrader/live/LiveTradeContextStore.hpp"
#include "daytrader/market_data/BarTimeframeTransformer.hpp"
#include "daytrader/market_data/InstrumentBarsMerger.hpp"
#include "daytrader/presentation/TerminalDashboard.hpp"
#include "daytrader/storage/MarketDataCsvStore.hpp"
#include "daytrader/time/TimeZoneFormatter.hpp"
#include "daytrader/universe/EtfUniverse.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <ranges>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>

namespace daytrader::monitoring {
namespace {

[[nodiscard]] int requested_history_days(const config::AppConfig& config)
{
    if (config.monitoring.history_lookback_days > 0) {
        return config.monitoring.history_lookback_days;
    }
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    const auto date = time::TimeZoneFormatter{config.time_zone}.format_date(now);
    const int year = std::stoi(date.substr(0, 4));
    const unsigned month = static_cast<unsigned>(std::stoi(date.substr(5, 2)));
    const unsigned day_of_month = static_cast<unsigned>(std::stoi(date.substr(8, 2)));
    using namespace std::chrono;
    const sys_days today = year_month_day{
        std::chrono::year{year},
        std::chrono::month{month},
        std::chrono::day{day_of_month},
    };
    const sys_days first = year_month_day{
        std::chrono::year{year},
        January,
        day{1},
    };
    return static_cast<int>((today - first).count()) + 1;
}

void wait_before_retry(
    std::chrono::seconds delay,
    const std::function<bool()>& stop_requested
)
{
    const auto deadline = std::chrono::steady_clock::now() + delay;
    while (!stop_requested() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{250});
    }
}

[[nodiscard]] std::vector<std::string> symbols_missing_coverage(
    const std::vector<domain::InstrumentBars>& history,
    const std::vector<config::HistoricalDataSettings>& requests,
    std::int64_t target_start
)
{
    std::vector<std::string> missing;
    for (const auto& request : requests) {
        const auto found = std::ranges::find(
            history,
            request.symbol,
            &domain::InstrumentBars::symbol
        );
        if (found == history.end() || found->bars.empty()
            || found->bars.front().epoch_seconds > target_start) {
            missing.push_back(request.symbol);
        }
    }
    return missing;
}

void backfill_minute_history(
    const config::AppConfig& config,
    const std::vector<config::HistoricalDataSettings>& request_templates,
    std::vector<domain::InstrumentBars>& history,
    const storage::MarketDataCsvStore& cache,
    std::mutex& history_mutex,
    const std::function<bool()>& stop_requested
)
{
    const int lookback_days = requested_history_days(config);
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    const auto target_start = now
        - static_cast<std::int64_t>(lookback_days) * 86'400;
    std::vector<std::string> missing;
    {
        const std::lock_guard lock{history_mutex};
        missing = symbols_missing_coverage(history, request_templates, target_start);
    }
    if (missing.empty()) {
        return;
    }

    std::clog << "Background backfill of complete " << lookback_days
              << "-day 1-minute history for " << missing.size()
              << " symbols into "
              << cache.directory().string() << '\n';
    for (const auto window : ibkr::plan_one_minute_day_windows(lookback_days)) {
        std::vector<config::HistoricalDataSettings> batch;
        batch.reserve(missing.size());
        for (const auto& request : request_templates) {
            if (std::ranges::find(missing, request.symbol) == missing.end()) {
                continue;
            }
            auto minute = request;
            minute.duration = std::to_string(window.duration_days) + " D";
            minute.bar_size = "1 min";
            minute.regular_trading_hours_only = false;
            minute.end_delay = std::chrono::hours{
                static_cast<std::int64_t>(window.end_delay_days) * 24
            };
            batch.push_back(std::move(minute));
        }

        std::clog << "Fetching background 1-minute IBKR batch: "
                  << window.duration_days << " days ending "
                  << window.end_delay_days << " days ago\n";
        std::vector<domain::InstrumentBars> received;
        while (!stop_requested()) {
            try {
                auto connection = config.ibkr;
                connection.client_id = config.monitoring.backfill_client_id;
                ibkr::TwsMarketDataClient client{std::move(connection)};
                received = client.fetch_historical_bars(batch, stop_requested);
                break;
            } catch (const std::exception& exception) {
                const bool pacing = ibkr::is_pacing_or_rate_limit_error(
                    exception.what()
                );
                const bool disconnected = ibkr::is_connection_interruption_error(
                    exception.what()
                );
                if (!pacing && !disconnected) {
                    throw;
                }
                std::cerr << (pacing
                        ? "IBKR pacing response received"
                        : "IBKR connection interrupted")
                          << "; retrying this complete batch after reconnect: "
                          << exception.what() << '\n';
                wait_before_retry(config.monitoring.reconnect_delay, stop_requested);
            }
        }
        if (stop_requested()) {
            return;
        }
        {
            const std::lock_guard lock{history_mutex};
            cache.merge(received);
            market_data::merge_instrument_bars(history, received);
            market_data::sort_and_deduplicate_bars(history);
        }
    }
}

} // namespace

MarketMonitor::MarketMonitor(config::AppConfig config)
    : config_{std::move(config)}
{
}

void MarketMonitor::run(const std::function<bool()>& stop_requested) const
{
    auto requests = universe::monitoring_data_requests(config_.etfs);
    for (auto& request : requests) {
        request.duration = config_.monitoring.history_duration;
        request.bar_size = "1 min";
        // Preserve and analyze pre/post-market bars instead of deleting them
        // before the strategy sees the source series.
        request.regular_trading_hours_only = false;
    }
    const auto synchronization_symbols = universe::signal_symbols(config_.etfs);
    std::vector<std::string> request_symbols;
    request_symbols.reserve(requests.size());
    for (const auto& request : requests) {
        request_symbols.push_back(request.symbol);
    }

    const storage::MarketDataCsvStore cache{config_.minute_data_directory};
    const auto history_start = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count() - static_cast<std::int64_t>(requested_history_days(config_)) * 86'400;
    auto history = cache.load_since(request_symbols, history_start);
    market_data::sort_and_deduplicate_bars(history);
    analysis::SetupCalibrationEngine setup_calibration{
        config_.minute_data_directory.parent_path() / "setup_outcomes.csv",
        config_.time_zone,
    };
    std::clog << "Loaded " << setup_calibration.record_count()
              << " BUILDING/READY outcome observations\n";
    std::mutex history_mutex;
    const analysis::MarketScanner scanner{
        config_.time_zone,
        config_.monitoring.bar_interval,
    };
    presentation::TerminalDashboard dashboard{config_.time_zone};
    live::LiveTradeContextStore live_context_store;
    std::mutex latest_scan_mutex;
    std::optional<domain::MarketScan> latest_scan;
    std::optional<std::int64_t> last_scan_timestamp;

    std::vector<config::HistoricalDataSettings> live_requests;
    for (const auto& etf : config_.etfs) {
        if (etf.market_data.symbol == "QQQ" || etf.market_data.symbol == "SOXX") {
            live_requests.push_back(etf.market_data);
        }
    }
    const auto day_trade_position_symbols = universe::day_trade_position_symbols(
        config_.etfs
    );

    dashboard.start();
    std::cout << "Continuous monitoring enabled: scan after each completed "
              << config_.monitoring.source_bar_interval.count()
              << "-second bar with " << config_.monitoring.bar_interval.count()
              << "-second trend context; Ctrl+C to stop\n";

    std::jthread live_thread{[&](std::stop_token token) {
        auto settings = config_.ibkr;
        settings.client_id = config_.monitoring.live_context_client_id;
        const auto live_stop = [&] {
            return token.stop_requested() || stop_requested();
        };
        while (!live_stop()) {
            try {
                ibkr::TwsLiveContextClient client{settings};
                client.monitor(
                    live_requests,
                    requests,
                    day_trade_position_symbols,
                    [&](domain::LiveTradeContext context) {
                        live_context_store.update(std::move(context));
                    },
                    live_stop
                );
            } catch (const std::exception& exception) {
                if (live_stop()) {
                    break;
                }
                std::cerr << "daytrader live context: " << exception.what() << '\n';
                wait_before_retry(config_.monitoring.reconnect_delay, live_stop);
            }
        }
    }};

    // Completed one-minute bars update execution state; streaming prices, P&L,
    // trade Delta, and Level-1 OFI refresh the dashboard every second.
    std::jthread refresh_thread{[&](std::stop_token token) {
        const analysis::LiveTradeContextEnricher enricher;
        while (!token.stop_requested() && !stop_requested()) {
            std::optional<domain::MarketScan> scan;
            {
                const std::lock_guard lock{latest_scan_mutex};
                scan = latest_scan;
            }
            if (scan.has_value()) {
                *scan = enricher.enrich(
                    std::move(*scan),
                    live_context_store.snapshot()
                );
                setup_calibration.enrich(*scan);
                dashboard.update(*scan);
            }
            for (int tenth = 0; tenth < 10 && !token.stop_requested()
                 && !stop_requested(); ++tenth) {
                std::this_thread::sleep_for(std::chrono::milliseconds{100});
            }
        }
    }};

    // Historical coverage runs independently on its own TWS client ID. It may
    // take hours or encounter pacing responses without delaying live bars,
    // streaming prices, Order Flow, positions, or terminal input.
    std::jthread backfill_thread{[&](std::stop_token token) {
        const auto backfill_stop = [&] {
            return token.stop_requested() || stop_requested();
        };
        try {
            backfill_minute_history(
                config_,
                requests,
                history,
                cache,
                history_mutex,
                backfill_stop
            );
        } catch (const std::exception& exception) {
            if (!backfill_stop()) {
                std::cerr << "daytrader background backfill: "
                          << exception.what() << '\n';
            }
        }
    }};

    while (!stop_requested()) {
        try {
            auto bar_connection = config_.ibkr;
            std::cout << "Connecting to IBKR TWS at " << config_.ibkr.host << ':'
                      << config_.ibkr.port << " (clientId=" << config_.ibkr.client_id << ")\n";
            std::cout << "Subscribing to " << requests.size()
                      << " signal and long-leveraged ETFs"
                      << " (short/inverse tickers are reference-only)\n";
            std::cout << "Merging live 1-minute bars with complete local history at "
                      << cache.directory().string() << '\n';

            ibkr::TwsMarketDataClient client{bar_connection};
            client.monitor_historical_bars(
                requests,
                synchronization_symbols,
                config_.monitoring.source_bar_interval,
                [&](const std::vector<domain::InstrumentBars>& bars) {
                    domain::MarketScan scan;
                    {
                        const std::lock_guard lock{history_mutex};
                        market_data::merge_instrument_bars(history, bars);
                        market_data::sort_and_deduplicate_bars(history);
                        const auto trend_bars = market_data::resample_bars(
                            history,
                            config_.monitoring.source_bar_interval,
                            config_.monitoring.bar_interval
                        );
                        scan = scanner.scan(
                            trend_bars,
                            history,
                            config_.etfs,
                            config_.monitoring.source_bar_interval
                        );
                    }
                    if (!last_scan_timestamp.has_value()
                        || scan.epoch_seconds > *last_scan_timestamp) {
                        try {
                            cache.merge(bars);
                        } catch (const std::exception& exception) {
                            // A cache write failure must not interrupt live risk
                            // information or force an IBKR reconnect.
                            std::cerr << "daytrader cache: " << exception.what() << '\n';
                        }
                        scan = analysis::LiveTradeContextEnricher{}.enrich(
                            std::move(scan),
                            live_context_store.snapshot()
                        );
                        try {
                            const std::lock_guard lock{history_mutex};
                            setup_calibration.observe_and_enrich(scan, history);
                        } catch (const std::exception& exception) {
                            // Calibration persistence is decision-support state;
                            // a local file problem must not interrupt live data.
                            std::cerr << "daytrader setup calibration: "
                                      << exception.what() << '\n';
                        }
                        {
                            const std::lock_guard lock{latest_scan_mutex};
                            latest_scan = scan;
                        }
                        dashboard.update(scan);
                        last_scan_timestamp = scan.epoch_seconds;
                    }
                },
                stop_requested
            );
        } catch (const std::exception& exception) {
            if (stop_requested()) {
                break;
            }
            std::cerr << "daytrader monitor: " << exception.what() << '\n';
            std::cerr << "Reconnecting in " << config_.monitoring.reconnect_delay.count()
                      << " seconds...\n";
            wait_before_retry(config_.monitoring.reconnect_delay, stop_requested);
        }
    }

    backfill_thread.request_stop();
    refresh_thread.request_stop();
    live_thread.request_stop();
    backfill_thread.join();
    refresh_thread.join();
    live_thread.join();
    dashboard.stop();
    std::cout << "Monitoring stopped\n";
}

} // namespace daytrader::monitoring
