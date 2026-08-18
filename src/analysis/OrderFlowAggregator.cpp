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
    std::size_t quote_count{};
    std::optional<double> first_trade_price;
    std::optional<double> last_trade_price;
};

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

    for (const auto& quote : quotes) {
        const double displayed_size = quote.bid_size + quote.ask_size;
        if (!std::isfinite(displayed_size) || displayed_size <= 0.0) {
            continue;
        }
        const auto start = bucket_start(quote.epoch_seconds, interval_seconds);
        auto& accumulator = buckets[start];
        accumulator.bar.epoch_seconds = start;
        accumulator.quote_imbalance_sum +=
            (quote.bid_size - quote.ask_size) / displayed_size * 100.0;
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
        }
        if (accumulator.first_trade_price.has_value()
            && accumulator.last_trade_price.has_value()
            && *accumulator.first_trade_price > 0.0) {
            bar.first_trade_price = accumulator.first_trade_price;
            bar.last_trade_price = accumulator.last_trade_price;
            bar.price_change_basis_points =
                (*accumulator.last_trade_price / *accumulator.first_trade_price - 1.0)
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
