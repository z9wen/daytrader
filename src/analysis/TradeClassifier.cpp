#include "daytrader/analysis/TradeClassifier.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <vector>

namespace daytrader::analysis {
namespace {

template <typename Tick>
[[nodiscard]] bool earlier(const Tick& left, const Tick& right)
{
    return left.epoch_seconds < right.epoch_seconds
        || (left.epoch_seconds == right.epoch_seconds
            && left.sequence < right.sequence);
}

} // namespace

TradeClassifier::TradeClassifier(TradeClassifierSettings settings)
    : settings_{settings}
{
    if (settings_.maximum_quote_age < std::chrono::seconds::zero()) {
        throw std::invalid_argument("maximum quote age cannot be negative");
    }
    if (settings_.price_epsilon < 0.0) {
        throw std::invalid_argument("trade-classifier epsilon cannot be negative");
    }
}

std::vector<domain::ClassifiedTrade> TradeClassifier::classify(
    std::span<const domain::TradeTick> trades,
    std::span<const domain::BidAskTick> quotes
) const
{
    std::vector<domain::TradeTick> ordered_trades{trades.begin(), trades.end()};
    std::vector<domain::BidAskTick> ordered_quotes{quotes.begin(), quotes.end()};
    std::ranges::stable_sort(ordered_trades, earlier<domain::TradeTick>);
    std::ranges::stable_sort(ordered_quotes, earlier<domain::BidAskTick>);

    std::vector<domain::ClassifiedTrade> result;
    result.reserve(ordered_trades.size());
    std::size_t quote_index{};
    const domain::BidAskTick* latest_quote{};
    std::optional<double> previous_trade_price;
    domain::TradeSide last_tick_direction{domain::TradeSide::unknown};

    for (const auto& trade : ordered_trades) {
        // Historical streams have one-second timestamps but no shared sequence.
        // Use the latest quote from the same second instead of discarding that
        // evidence; live classification still follows exact callback order.
        while (quote_index < ordered_quotes.size()
               && ordered_quotes[quote_index].epoch_seconds <= trade.epoch_seconds) {
            latest_quote = &ordered_quotes[quote_index++];
        }

        domain::TradeSide side{domain::TradeSide::unknown};
        domain::TradeClassificationMethod method{
            domain::TradeClassificationMethod::unknown
        };
        const bool valid_trade = std::isfinite(trade.price) && trade.price > 0.0
            && std::isfinite(trade.size) && trade.size > 0.0;
        const bool fresh_quote = latest_quote != nullptr
            && trade.epoch_seconds - latest_quote->epoch_seconds
                <= settings_.maximum_quote_age.count()
            && latest_quote->bid_price > 0.0
            && latest_quote->ask_price >= latest_quote->bid_price;

        if (valid_trade && fresh_quote
            && trade.price >= latest_quote->ask_price - settings_.price_epsilon) {
            side = domain::TradeSide::buy;
            method = domain::TradeClassificationMethod::quote_test;
        } else if (valid_trade && fresh_quote
                   && trade.price <= latest_quote->bid_price + settings_.price_epsilon) {
            side = domain::TradeSide::sell;
            method = domain::TradeClassificationMethod::quote_test;
        } else if (valid_trade && previous_trade_price.has_value()) {
            if (trade.price > *previous_trade_price + settings_.price_epsilon) {
                side = domain::TradeSide::buy;
            } else if (trade.price < *previous_trade_price - settings_.price_epsilon) {
                side = domain::TradeSide::sell;
            } else {
                // The tick rule carries the last non-zero direction across equal prints.
                side = last_tick_direction;
            }
            if (side != domain::TradeSide::unknown) {
                method = domain::TradeClassificationMethod::tick_rule;
            }
        }

        if (valid_trade) {
            previous_trade_price = trade.price;
        }
        if (side != domain::TradeSide::unknown) {
            last_tick_direction = side;
        }
        result.push_back(domain::ClassifiedTrade{
            .trade = trade,
            .side = side,
            .method = method,
        });
    }
    return result;
}

} // namespace daytrader::analysis
