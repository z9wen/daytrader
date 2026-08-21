#pragma once

#include "daytrader/config/MarketDataSettings.hpp"
#include "daytrader/domain/InstrumentBars.hpp"
#include "daytrader/domain/TradingSchedule.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace daytrader::ibkr {

// RAII wrapper around the callback-based official TWS C++ API.
// It normalizes historical updates into domain bars and publishes synchronized scans.
class TwsMarketDataClient {
public:
    explicit TwsMarketDataClient(config::IbkrConnectionSettings settings);
    ~TwsMarketDataClient();

    TwsMarketDataClient(const TwsMarketDataClient&) = delete;
    TwsMarketDataClient& operator=(const TwsMarketDataClient&) = delete;
    TwsMarketDataClient(TwsMarketDataClient&&) noexcept;
    TwsMarketDataClient& operator=(TwsMarketDataClient&&) noexcept;

    // One-shot historical fetch, primarily useful for diagnostics and tests.
    [[nodiscard]] std::vector<domain::InstrumentBars> fetch_historical_bars(
        const std::vector<config::HistoricalDataSettings>& requests,
        const std::function<bool()>& stop_requested = {}
    );

    // One-shot IBKR whatToShow=SCHEDULE requests. Results retain request order
    // so callers can safely issue several year windows for the same symbol.
    [[nodiscard]] std::vector<domain::TradingSchedule> fetch_historical_schedules(
        const std::vector<config::HistoricalDataSettings>& requests,
        const std::function<bool()>& stop_requested = {}
    );

    // Keeps subscriptions open and invokes the handler once per common completed bar.
    void monitor_historical_bars(
        const std::vector<config::HistoricalDataSettings>& requests,
        const std::vector<std::string>& synchronization_symbols,
        std::chrono::seconds bar_interval,
        const std::function<void(const std::vector<domain::InstrumentBars>&)>& on_completed_bar,
        const std::function<bool()>& stop_requested
    );

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace daytrader::ibkr
