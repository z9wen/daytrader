#include "daytrader/monitoring/MarketMonitor.hpp"

#include "daytrader/analysis/MarketScanner.hpp"
#include "daytrader/analysis/LiveTradeContextEnricher.hpp"
#include "daytrader/ibkr/TwsLiveContextClient.hpp"
#include "daytrader/ibkr/TwsMarketDataClient.hpp"
#include "daytrader/live/LiveTradeContextStore.hpp"
#include "daytrader/market_data/InstrumentBarsMerger.hpp"
#include "daytrader/presentation/TerminalDashboard.hpp"
#include "daytrader/storage/MarketDataCsvStore.hpp"
#include "daytrader/universe/EtfUniverse.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>

namespace daytrader::monitoring {
namespace {

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
        request.maximum_bars = config_.monitoring.history_maximum_bars;
    }
    const auto synchronization_symbols = universe::signal_symbols(config_.etfs);
    std::vector<std::string> request_symbols;
    request_symbols.reserve(requests.size());
    for (const auto& request : requests) {
        request_symbols.push_back(request.symbol);
    }

    const storage::MarketDataCsvStore cache{config_.data_directory};
    auto history = cache.load(request_symbols);
    market_data::sort_and_deduplicate_bars(history);
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
              << config_.monitoring.bar_interval.count() << "-second bar; Ctrl+C to stop\n";

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

    // Historical bars change only at completed five-minute boundaries, while
    // P&L and DeltaRatio remain useful at one-second dashboard cadence.
    std::jthread refresh_thread{[&](std::stop_token token) {
        const analysis::LiveTradeContextEnricher enricher;
        while (!token.stop_requested() && !stop_requested()) {
            std::optional<domain::MarketScan> scan;
            {
                const std::lock_guard lock{latest_scan_mutex};
                scan = latest_scan;
            }
            if (scan.has_value()) {
                scan->live_context = enricher.enrich(
                    *scan,
                    live_context_store.snapshot()
                );
                dashboard.update(*scan);
            }
            for (int tenth = 0; tenth < 10 && !token.stop_requested()
                 && !stop_requested(); ++tenth) {
                std::this_thread::sleep_for(std::chrono::milliseconds{100});
            }
        }
    }};

    while (!stop_requested()) {
        try {
            auto bar_connection = config_.ibkr;
            bar_connection.request_timeout = config_.monitoring.initial_data_timeout;
            std::cout << "Connecting to IBKR TWS at " << config_.ibkr.host << ':'
                      << config_.ibkr.port << " (clientId=" << config_.ibkr.client_id << ")\n";
            std::cout << "Subscribing to " << requests.size()
                      << " signal and long-leveraged ETFs"
                      << " (short/inverse tickers are reference-only)\n";
            std::cout << "Merging the latest " << config_.monitoring.history_duration
                      << " with local history at " << cache.directory().string() << '\n';

            ibkr::TwsMarketDataClient client{bar_connection};
            client.monitor_historical_bars(
                requests,
                synchronization_symbols,
                config_.monitoring.bar_interval,
                [&](const std::vector<domain::InstrumentBars>& bars) {
                    // The small live request keeps startup fast. Merging it with
                    // the durable cache supplies the prior sessions needed by
                    // time-of-day RVOL without repeatedly downloading them.
                    market_data::merge_instrument_bars(history, bars);
                    market_data::sort_and_deduplicate_bars(history);
                    auto scan = scanner.scan(history, config_.etfs);
                    if (!last_scan_timestamp.has_value()
                        || scan.epoch_seconds > *last_scan_timestamp) {
                        try {
                            cache.save(history);
                        } catch (const std::exception& exception) {
                            // A cache write failure must not interrupt live risk
                            // information or force an IBKR reconnect.
                            std::cerr << "daytrader cache: " << exception.what() << '\n';
                        }
                        {
                            const std::lock_guard lock{latest_scan_mutex};
                            latest_scan = scan;
                        }
                        scan.live_context = analysis::LiveTradeContextEnricher{}.enrich(
                            scan,
                            live_context_store.snapshot()
                        );
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

    refresh_thread.request_stop();
    live_thread.request_stop();
    refresh_thread.join();
    live_thread.join();
    dashboard.stop();
    std::cout << "Monitoring stopped\n";
}

} // namespace daytrader::monitoring
