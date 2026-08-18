#include "daytrader/live/LiveOrderFlowTracker.hpp"

#include "daytrader/analysis/OrderFlowWindowAnalyzer.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

namespace daytrader::live {

LiveOrderFlowTracker::LiveOrderFlowTracker(
    std::string symbol,
    LiveOrderFlowSettings settings
)
    : symbol_{std::move(symbol)}
    , settings_{settings}
    , time_formatter_{settings_.time_zone}
{
    if (symbol_.empty()) {
        throw std::invalid_argument("live Order Flow symbol cannot be empty");
    }
    if (settings_.maximum_quote_age < std::chrono::seconds::zero()
        || settings_.retained_history < std::chrono::seconds{60}
        || settings_.price_epsilon < 0.0) {
        throw std::invalid_argument("live Order Flow settings are invalid");
    }
}

void LiveOrderFlowTracker::on_quote(
    std::int64_t epoch_seconds,
    double bid_price,
    double ask_price,
    double bid_size,
    double ask_size
)
{
    if (!accepts_timestamp(epoch_seconds)) {
        return;
    }
    if (!std::isfinite(bid_price) || !std::isfinite(ask_price)
        || bid_price <= 0.0 || ask_price < bid_price) {
        return;
    }
    domain::BidAskTick quote{
        .epoch_seconds = epoch_seconds,
        .sequence = sequence_++,
        .bid_price = bid_price,
        .ask_price = ask_price,
        .bid_size = std::max(0.0, bid_size),
        .ask_size = std::max(0.0, ask_size),
    };
    latest_quote_ = quote;
    quotes_.push_back(std::move(quote));
    prune(epoch_seconds);
}

void LiveOrderFlowTracker::on_trade(
    std::int64_t epoch_seconds,
    double price,
    double size
)
{
    if (!accepts_timestamp(epoch_seconds)) {
        return;
    }
    if (!std::isfinite(price) || price <= 0.0
        || !std::isfinite(size) || size <= 0.0) {
        return;
    }

    domain::TradeSide side{domain::TradeSide::unknown};
    domain::TradeClassificationMethod method{
        domain::TradeClassificationMethod::unknown
    };
    const bool fresh_quote = latest_quote_.has_value()
        && epoch_seconds >= latest_quote_->epoch_seconds
        && epoch_seconds - latest_quote_->epoch_seconds
            <= settings_.maximum_quote_age.count();
    if (fresh_quote && price >= latest_quote_->ask_price - settings_.price_epsilon) {
        side = domain::TradeSide::buy;
        method = domain::TradeClassificationMethod::quote_test;
    } else if (fresh_quote
               && price <= latest_quote_->bid_price + settings_.price_epsilon) {
        side = domain::TradeSide::sell;
        method = domain::TradeClassificationMethod::quote_test;
    } else if (previous_trade_price_.has_value()) {
        if (price > *previous_trade_price_ + settings_.price_epsilon) {
            side = domain::TradeSide::buy;
        } else if (price < *previous_trade_price_ - settings_.price_epsilon) {
            side = domain::TradeSide::sell;
        } else {
            side = last_tick_direction_;
        }
        if (side != domain::TradeSide::unknown) {
            method = domain::TradeClassificationMethod::tick_rule;
        }
    }

    previous_trade_price_ = price;
    if (side != domain::TradeSide::unknown) {
        last_tick_direction_ = side;
    }
    trades_.push_back(domain::ClassifiedTrade{
        .trade = domain::TradeTick{
            .epoch_seconds = epoch_seconds,
            .sequence = sequence_++,
            .price = price,
            .size = size,
        },
        .side = side,
        .method = method,
    });
    prune(epoch_seconds);
}

domain::LiveOrderFlowSnapshot LiveOrderFlowTracker::snapshot(
    std::int64_t epoch_seconds
) const
{
    const std::vector<domain::ClassifiedTrade> trades{trades_.begin(), trades_.end()};
    const std::vector<domain::BidAskTick> quotes{quotes_.begin(), quotes_.end()};
    const analysis::OrderFlowWindowAnalyzer analyzer;
    return domain::LiveOrderFlowSnapshot{
        .symbol = symbol_,
        .updated_epoch_seconds = epoch_seconds,
        .thirty_seconds = analyzer.analyze(
            trades,
            quotes,
            epoch_seconds - 30,
            epoch_seconds
        ),
        .one_minute = analyzer.analyze(
            trades,
            quotes,
            epoch_seconds - 60,
            epoch_seconds
        ),
    };
}

bool LiveOrderFlowTracker::accepts_timestamp(std::int64_t epoch_seconds) const
{
    if (!settings_.regular_trading_hours_only) {
        return true;
    }
    const int minute = time_formatter_.minutes_since_midnight(epoch_seconds);
    return minute >= 9 * 60 + 30 && minute < 16 * 60;
}

void LiveOrderFlowTracker::prune(std::int64_t epoch_seconds)
{
    const auto cutoff = epoch_seconds - settings_.retained_history.count();
    while (!trades_.empty() && trades_.front().trade.epoch_seconds < cutoff) {
        trades_.pop_front();
    }
    while (!quotes_.empty() && quotes_.front().epoch_seconds < cutoff) {
        quotes_.pop_front();
    }
}

} // namespace daytrader::live
