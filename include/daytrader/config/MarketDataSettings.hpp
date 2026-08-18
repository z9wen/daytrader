#pragma once

#include <cstddef>
#include <chrono>
#include <string>

namespace daytrader::config {

// Socket connection parameters for a local TWS or IB Gateway session.
struct IbkrConnectionSettings {
    std::string host{"127.0.0.1"};
    int port{9972};
    int client_id{7};
    std::chrono::seconds request_timeout{30};
};

// Describes one reqHistoricalData subscription, including its IB contract identity.
struct HistoricalDataSettings {
    std::string symbol;
    std::string security_type{"STK"};
    std::string exchange{"SMART"};
    std::string primary_exchange;
    std::string currency{"USD"};
    std::string duration{"2 D"};
    std::string bar_size{"5 mins"};
    std::string data_type{"TRADES"};
    bool regular_trading_hours_only{true};
    std::chrono::minutes end_delay{0};
    // Optional requests, such as VIX, may fail without stopping the ETF monitor.
    bool required{true};
    // Monitoring needs only recent context; research requests may opt into a larger history.
    std::size_t maximum_bars{2'048};
};

} // namespace daytrader::config
