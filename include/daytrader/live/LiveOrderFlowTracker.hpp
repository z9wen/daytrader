#pragma once

#include "daytrader/domain/LiveTradeContext.hpp"
#include "daytrader/time/TimeZoneFormatter.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace daytrader::live {

struct LiveOrderFlowSettings {
    std::chrono::seconds maximum_quote_age{2};
    double price_epsilon{1e-8};
    std::string time_zone{"America/New_York"};
    bool regular_trading_hours_only{};
};

// Incremental quote-test/tick-rule classifier for one live symbol. Raw events
// are retained for the lifetime of the tracker; analysis windows select the
// relevant observations without deleting the source stream.
class LiveOrderFlowTracker {
public:
    explicit LiveOrderFlowTracker(
        std::string symbol,
        LiveOrderFlowSettings settings = {}
    );

    void on_quote(
        std::int64_t epoch_seconds,
        double bid_price,
        double ask_price,
        double bid_size,
        double ask_size
    );
    void on_trade(std::int64_t epoch_seconds, double price, double size);

    [[nodiscard]] domain::LiveOrderFlowSnapshot snapshot(
        std::int64_t epoch_seconds
    ) const;

private:
    [[nodiscard]] bool accepts_timestamp(std::int64_t epoch_seconds) const;

    std::string symbol_;
    LiveOrderFlowSettings settings_;
    time::TimeZoneFormatter time_formatter_;
    std::deque<domain::ClassifiedTrade> trades_;
    std::deque<domain::BidAskTick> quotes_;
    std::optional<domain::BidAskTick> latest_quote_;
    std::optional<double> previous_trade_price_;
    domain::TradeSide last_tick_direction_{domain::TradeSide::unknown};
    std::size_t sequence_{};
};

} // namespace daytrader::live
