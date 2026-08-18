#pragma once

#include "daytrader/config/MarketDataSettings.hpp"
#include "daytrader/domain/OrderFlow.hpp"

#include <cstdint>
#include <memory>

namespace daytrader::ibkr {

struct HistoricalTickRequest {
    config::HistoricalDataSettings contract;
    std::int64_t end_timestamp{};
    int number_of_ticks{1'000};
    int minimum_lookback_seconds{60};
    int maximum_pages_per_stream{6};
};

// Fetches the most recent trade and bid/ask events ending at one candidate
// entry. Bounded backward paging reaches the requested lookback without the
// pacing cost of full-day tick replay.
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
