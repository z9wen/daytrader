#pragma once

#include "daytrader/config/MarketDataSettings.hpp"
#include "daytrader/domain/OrderFlow.hpp"

#include <cstdint>
#include <memory>

namespace daytrader::ibkr {

// reqHistoricalTicks documents 1,000 as the maximum result count per request.
inline constexpr int historical_tick_maximum_results = 1'000;

struct HistoricalTickRequest {
    config::HistoricalDataSettings contract;
    std::int64_t end_timestamp{};
    int number_of_ticks{historical_tick_maximum_results};
    int minimum_lookback_seconds{60};
};

// Fetches trade and bid/ask events ending at one candidate entry. Backward
// paging continues until both streams cover the requested lookback. Each next
// page is submitted immediately; TWS/IBKR remains the authority on pacing.
class TwsHistoricalTickClient {
public:
    explicit TwsHistoricalTickClient(config::IbkrConnectionSettings settings);
    ~TwsHistoricalTickClient();

    TwsHistoricalTickClient(const TwsHistoricalTickClient&) = delete;
    TwsHistoricalTickClient& operator=(const TwsHistoricalTickClient&) = delete;
    TwsHistoricalTickClient(TwsHistoricalTickClient&&) noexcept;
    TwsHistoricalTickClient& operator=(TwsHistoricalTickClient&&) noexcept;

    [[nodiscard]] domain::OrderFlowTicks fetch(const HistoricalTickRequest& request);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace daytrader::ibkr
