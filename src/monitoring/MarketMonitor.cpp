#include "daytrader/monitoring/MarketMonitor.hpp"

#include "daytrader/analysis/MarketScanner.hpp"
#include "daytrader/ibkr/TwsMarketDataClient.hpp"
#include "daytrader/presentation/TerminalDashboard.hpp"
#include "daytrader/universe/EtfUniverse.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <optional>
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
    const auto requests = universe::monitoring_data_requests(config_.etfs);
    const auto synchronization_symbols = universe::signal_symbols(config_.etfs);
    const analysis::MarketScanner scanner{
        config_.time_zone,
        config_.monitoring.bar_interval,
    };
    presentation::TerminalDashboard dashboard{config_.time_zone};
    std::optional<std::int64_t> last_scan_timestamp;

    dashboard.start();
    std::cout << "Continuous monitoring enabled: scan after each completed "
              << config_.monitoring.bar_interval.count() << "-second bar; Ctrl+C to stop\n";

    while (!stop_requested()) {
        try {
            std::cout << "Connecting to IBKR TWS at " << config_.ibkr.host << ':'
                      << config_.ibkr.port << " (clientId=" << config_.ibkr.client_id << ")\n";
            std::cout << "Subscribing to " << requests.size()
                      << " signal and long-leveraged ETFs"
                      << " (short/inverse tickers are reference-only)\n";

            ibkr::TwsMarketDataClient client{config_.ibkr};
            client.monitor_historical_bars(
                requests,
                synchronization_symbols,
                config_.monitoring.bar_interval,
                [&](const std::vector<domain::InstrumentBars>& bars) {
                    const auto scan = scanner.scan(bars, config_.etfs);
                    if (!last_scan_timestamp.has_value()
                        || scan.epoch_seconds > *last_scan_timestamp) {
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

    dashboard.stop();
    std::cout << "Monitoring stopped\n";
}

} // namespace daytrader::monitoring
