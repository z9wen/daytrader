#include "daytrader/analysis/OrderFlowAggregator.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace daytrader::analysis {
namespace {

struct Accumulator {
    domain::OrderFlowBar bar;
    double quote_classified_volume{};
    double quote_imbalance_sum{};
    double ofi_activity{};
    double spread_basis_points_sum{};
    std::size_t quote_count{};
    std::optional<double> first_trade_price;
    std::optional<double> last_trade_price;
};

[[nodiscard]] bool valid_quote(const domain::BidAskTick& quote)
{
    return std::isfinite(quote.bid_price) && std::isfinite(quote.ask_price)
        && std::isfinite(quote.bid_size) && std::isfinite(quote.ask_size)
        && quote.bid_price > 0.0 && quote.ask_price >= quote.bid_price
        && quote.bid_size >= 0.0 && quote.ask_size >= 0.0;
}

[[nodiscard]] double bid_ofi(
    const domain::BidAskTick& previous,
    const domain::BidAskTick& current
)
{
    return current.bid_price > previous.bid_price ? current.bid_size
        : (current.bid_price < previous.bid_price ? -previous.bid_size
                                                   : current.bid_size - previous.bid_size);
}

[[nodiscard]] double ask_ofi(
    const domain::BidAskTick& previous,
    const domain::BidAskTick& current
)
{
    return current.ask_price > previous.ask_price ? previous.ask_size
        : (current.ask_price < previous.ask_price ? -current.ask_size
                                                   : previous.ask_size - current.ask_size);
}

[[nodiscard]] std::int64_t bucket_start(
    std::int64_t timestamp,
    std::int64_t interval_seconds
)
{
    // IBKR equity timestamps are positive, but keep floor semantics explicit.
    const auto remainder = timestamp % interval_seconds;
    return remainder < 0
        ? timestamp - remainder - interval_seconds
        : timestamp - remainder;
}

} // namespace

OrderFlowAggregator::OrderFlowAggregator(std::chrono::seconds interval)
    : interval_{interval}
{
    if (interval_ <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("order-flow interval must be positive");
    }
}

std::vector<domain::OrderFlowBar> OrderFlowAggregator::aggregate(
    std::span<const domain::ClassifiedTrade> trades,
    std::span<const domain::BidAskTick> quotes
) const
{
    std::map<std::int64_t, Accumulator> buckets;
    const auto interval_seconds = interval_.count();

    for (const auto& classified : trades) {
        if (!std::isfinite(classified.trade.size) || classified.trade.size <= 0.0) {
            continue;
        }
        const auto start = bucket_start(
            classified.trade.epoch_seconds,
            interval_seconds
        );
        auto& accumulator = buckets[start];
        accumulator.bar.epoch_seconds = start;
        if (!accumulator.first_trade_price.has_value()) {
            accumulator.first_trade_price = classified.trade.price;
        }
        accumulator.last_trade_price = classified.trade.price;
        ++accumulator.bar.trade_count;
        switch (classified.side) {
        case domain::TradeSide::buy:
            accumulator.bar.buy_volume += classified.trade.size;
            break;
        case domain::TradeSide::sell:
            accumulator.bar.sell_volume += classified.trade.size;
            break;
        case domain::TradeSide::unknown:
            accumulator.bar.unknown_volume += classified.trade.size;
            break;
        }
        if (classified.method == domain::TradeClassificationMethod::quote_test) {
            accumulator.quote_classified_volume += classified.trade.size;
        }
    }

    std::optional<domain::BidAskTick> previous_quote;
    for (const auto& quote : quotes) {
        if (!valid_quote(quote)) {
            continue;
        }
        const double displayed_size = quote.bid_size + quote.ask_size;
        const auto start = bucket_start(quote.epoch_seconds, interval_seconds);
        auto& accumulator = buckets[start];
        accumulator.bar.epoch_seconds = start;
        const double midpoint = (quote.bid_price + quote.ask_price) / 2.0;
        if (!accumulator.bar.first_midpoint_price.has_value()) {
            accumulator.bar.first_midpoint_price = midpoint;
        }
        accumulator.bar.last_midpoint_price = midpoint;
        if (displayed_size > 0.0) {
            accumulator.quote_imbalance_sum +=
                (quote.bid_size - quote.ask_size) / displayed_size * 100.0;
            const double microprice = (
                quote.ask_price * quote.bid_size
                + quote.bid_price * quote.ask_size
            ) / displayed_size;
            accumulator.bar.microprice_skew_basis_points = midpoint > 0.0
                ? std::optional<double>{(microprice / midpoint - 1.0) * 10'000.0}
                : std::nullopt;
        }
        if (midpoint > 0.0) {
            accumulator.spread_basis_points_sum +=
                (quote.ask_price - quote.bid_price) / midpoint * 10'000.0;
        }
        if (previous_quote.has_value()) {
            const double bid_event = bid_ofi(*previous_quote, quote);
            const double ask_event = ask_ofi(*previous_quote, quote);
            accumulator.bar.level1_ofi += bid_event + ask_event;
            accumulator.ofi_activity += std::abs(bid_event) + std::abs(ask_event);
        }
        previous_quote = quote;
        ++accumulator.quote_count;
    }

    std::vector<domain::OrderFlowBar> result;
    result.reserve(buckets.size());
    for (auto& [unused, accumulator] : buckets) {
        static_cast<void>(unused);
        auto& bar = accumulator.bar;
        const double classified_volume = bar.buy_volume + bar.sell_volume;
        const double total_volume = classified_volume + bar.unknown_volume;
        bar.delta = bar.buy_volume - bar.sell_volume;
        if (classified_volume > 0.0) {
            bar.delta_ratio_percent = bar.delta / classified_volume * 100.0;
        }
        if (total_volume > 0.0) {
            bar.classification_coverage_percent =
                classified_volume / total_volume * 100.0;
            bar.quote_test_coverage_percent =
                accumulator.quote_classified_volume / total_volume * 100.0;
        }
        if (accumulator.quote_count > 0) {
            bar.average_quote_imbalance_percent = accumulator.quote_imbalance_sum
                / static_cast<double>(accumulator.quote_count);
            bar.average_spread_basis_points = accumulator.spread_basis_points_sum
                / static_cast<double>(accumulator.quote_count);
        }
        if (accumulator.ofi_activity > 0.0) {
            bar.level1_ofi_ratio_percent = std::clamp(
                bar.level1_ofi / accumulator.ofi_activity * 100.0,
                -100.0,
                100.0
            );
        }
        if (accumulator.first_trade_price.has_value()
            && accumulator.last_trade_price.has_value()
            && *accumulator.first_trade_price > 0.0) {
            bar.first_trade_price = accumulator.first_trade_price;
            bar.last_trade_price = accumulator.last_trade_price;
            bar.price_change_basis_points =
                (*accumulator.last_trade_price / *accumulator.first_trade_price - 1.0)
                * 10'000.0;
        } else if (bar.first_midpoint_price.has_value()
                   && bar.last_midpoint_price.has_value()
                   && *bar.first_midpoint_price > 0.0) {
            bar.price_change_basis_points =
                (*bar.last_midpoint_price / *bar.first_midpoint_price - 1.0)
                * 10'000.0;
        }
        if (bar.price_change_basis_points.has_value()
            && bar.delta_ratio_percent.has_value()
            && std::abs(*bar.delta_ratio_percent) > 1e-9) {
            bar.impact_efficiency = *bar.price_change_basis_points
                / std::abs(*bar.delta_ratio_percent);
        }
        result.push_back(bar);
    }
    return result;
}

} // namespace daytrader::analysis
