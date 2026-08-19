#pragma once

#include "daytrader/config/MarketDataSettings.hpp"
#include "daytrader/domain/LiveTradeContext.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace daytrader::ibkr {

// A separate TWS connection for positions/P&L, all-universe Level-1 prices,
// and the four tick-by-tick streams used by QQQ and SOXX Order Flow.
class TwsLiveContextClient {
public:
    explicit TwsLiveContextClient(config::IbkrConnectionSettings settings);
    ~TwsLiveContextClient();

    TwsLiveContextClient(const TwsLiveContextClient&) = delete;
    TwsLiveContextClient& operator=(const TwsLiveContextClient&) = delete;
    TwsLiveContextClient(TwsLiveContextClient&&) noexcept;
    TwsLiveContextClient& operator=(TwsLiveContextClient&&) noexcept;

    void monitor(
        const std::vector<config::HistoricalDataSettings>& order_flow_symbols,
        const std::vector<config::HistoricalDataSettings>& market_data_symbols,
        const std::vector<std::string>& position_symbols,
        const std::function<void(domain::LiveTradeContext)>& on_update,
        const std::function<bool()>& stop_requested
    );

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace daytrader::ibkr
