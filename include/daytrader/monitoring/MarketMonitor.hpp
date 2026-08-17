#pragma once

#include "daytrader/config/AppConfig.hpp"

#include <functional>

namespace daytrader::monitoring {

// Owns the reconnect loop and connects market-data callbacks to analysis and display.
class MarketMonitor {
public:
    explicit MarketMonitor(config::AppConfig config);

    // Runs until the supplied predicate observes SIGINT/SIGTERM or another stop source.
    void run(const std::function<bool()>& stop_requested) const;

private:
    config::AppConfig config_;
};

} // namespace daytrader::monitoring
