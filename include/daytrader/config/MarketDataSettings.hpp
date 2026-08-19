#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace daytrader::config {

// Socket connection parameters for a local TWS or IB Gateway session.
struct IbkrConnectionSettings {
    std::string host{"127.0.0.1"};
    int port{9972};
    int client_id{7};
};

// Describes one reqHistoricalData subscription, including its IB contract identity.
struct HistoricalDataSettings {
    std::string symbol;
    std::string security_type{"STK"};
    std::string exchange{"SMART"};
    std::string primary_exchange;
    std::string currency{"USD"};
    std::string duration{"2 D"};
    std::string bar_size{"1 min"};
    std::string data_type{"TRADES"};
    bool regular_trading_hours_only{false};
    std::chrono::minutes end_delay{0};
    // An exact UTC endpoint is used by durable daily backfills. When absent,
    // end_delay retains the relative-time behavior used by live refreshes.
    std::optional<std::int64_t> end_timestamp;
    // Optional requests, such as VIX, may fail without stopping the ETF monitor.
    bool required{true};
};

} // namespace daytrader::config
