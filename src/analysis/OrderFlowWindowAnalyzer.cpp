#include "daytrader/analysis/OrderFlowWindowAnalyzer.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace daytrader::analysis {
namespace {

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
    if (current.bid_price > previous.bid_price) {
        return current.bid_size;
    }
    if (current.bid_price < previous.bid_price) {
        return -previous.bid_size;
    }
    return current.bid_size - previous.bid_size;
}

[[nodiscard]] double ask_ofi(
    const domain::BidAskTick& previous,
    const domain::BidAskTick& current
)
{
    if (current.ask_price > previous.ask_price) {
        return previous.ask_size;
    }
    if (current.ask_price < previous.ask_price) {
        return -current.ask_size;
    }
    return previous.ask_size - current.ask_size;
}

} // namespace

domain::OrderFlowWindow OrderFlowWindowAnalyzer::analyze(
    std::span<const domain::ClassifiedTrade> trades,
    std::span<const domain::BidAskTick> quotes,
    std::int64_t start_timestamp,
    std::int64_t end_timestamp
) const
{
    if (start_timestamp > end_timestamp) {
        throw std::invalid_argument("order-flow window start must not exceed end");
    }

    domain::OrderFlowWindow result{
        .start_timestamp = start_timestamp,
        .end_timestamp = end_timestamp,
    };
    result.flow.epoch_seconds = start_timestamp;
    double quote_classified_volume{};
    double quote_imbalance_sum{};
    double ofi_activity{};
    double spread_basis_points_sum{};
    bool trade_history_reaches_start{};
    bool quote_history_reaches_start{};
    std::optional<double> first_trade_price;
    std::optional<double> last_trade_price;
    std::optional<domain::BidAskTick> previous_quote;

    for (const auto& classified : trades) {
        trade_history_reaches_start = trade_history_reaches_start
            || classified.trade.epoch_seconds <= start_timestamp;
        if (classified.trade.epoch_seconds < start_timestamp
            || classified.trade.epoch_seconds > end_timestamp
            || !std::isfinite(classified.trade.size)
            || classified.trade.size <= 0.0) {
            continue;
        }
        if (!first_trade_price.has_value()) {
            first_trade_price = classified.trade.price;
        }
        last_trade_price = classified.trade.price;
        ++result.flow.trade_count;
        switch (classified.side) {
        case domain::TradeSide::buy:
            result.flow.buy_volume += classified.trade.size;
            break;
        case domain::TradeSide::sell:
            result.flow.sell_volume += classified.trade.size;
            break;
        case domain::TradeSide::unknown:
            result.flow.unknown_volume += classified.trade.size;
            break;
        }
        if (classified.method == domain::TradeClassificationMethod::quote_test) {
            quote_classified_volume += classified.trade.size;
        }
    }

    for (const auto& quote : quotes) {
        quote_history_reaches_start = quote_history_reaches_start
            || quote.epoch_seconds <= start_timestamp;
        if (!valid_quote(quote)) {
            continue;
        }
        if (quote.epoch_seconds < start_timestamp) {
            previous_quote = quote;
            continue;
        }
        if (quote.epoch_seconds > end_timestamp) {
            continue;
        }
        const double displayed_size = quote.bid_size + quote.ask_size;
        const double midpoint = (quote.bid_price + quote.ask_price) / 2.0;
        if (!result.flow.first_midpoint_price.has_value()) {
            result.flow.first_midpoint_price = midpoint;
        }
        result.flow.last_midpoint_price = midpoint;
        if (displayed_size > 0.0) {
            quote_imbalance_sum +=
                (quote.bid_size - quote.ask_size) / displayed_size * 100.0;
            const double microprice = (
                quote.ask_price * quote.bid_size
                + quote.bid_price * quote.ask_size
            ) / displayed_size;
            result.flow.microprice_skew_basis_points = midpoint > 0.0
                ? std::optional<double>{(microprice / midpoint - 1.0) * 10'000.0}
                : std::nullopt;
        }
        if (midpoint > 0.0) {
            spread_basis_points_sum +=
                (quote.ask_price - quote.bid_price) / midpoint * 10'000.0;
        }
        if (previous_quote.has_value()) {
            const double bid_event = bid_ofi(*previous_quote, quote);
            const double ask_event = ask_ofi(*previous_quote, quote);
            result.flow.level1_ofi += bid_event + ask_event;
            ofi_activity += std::abs(bid_event) + std::abs(ask_event);
        }
        previous_quote = quote;
        ++result.quote_count;
    }

    auto& flow = result.flow;
    const double classified_volume = flow.buy_volume + flow.sell_volume;
    const double total_volume = classified_volume + flow.unknown_volume;
    flow.delta = flow.buy_volume - flow.sell_volume;
    if (classified_volume > 0.0) {
        flow.delta_ratio_percent = flow.delta / classified_volume * 100.0;
    }
    if (total_volume > 0.0) {
        flow.classification_coverage_percent =
            classified_volume / total_volume * 100.0;
        flow.quote_test_coverage_percent = quote_classified_volume / total_volume * 100.0;
    }
    if (result.quote_count > 0) {
        flow.average_quote_imbalance_percent = quote_imbalance_sum
            / static_cast<double>(result.quote_count);
        flow.average_spread_basis_points = spread_basis_points_sum
            / static_cast<double>(result.quote_count);
    }
    if (ofi_activity > 0.0) {
        flow.level1_ofi_ratio_percent = std::clamp(
            flow.level1_ofi / ofi_activity * 100.0,
            -100.0,
            100.0
        );
    }
    if (first_trade_price.has_value() && last_trade_price.has_value()
        && *first_trade_price > 0.0) {
        flow.first_trade_price = first_trade_price;
        flow.last_trade_price = last_trade_price;
        flow.price_change_basis_points =
            (*last_trade_price / *first_trade_price - 1.0) * 10'000.0;
    } else if (flow.first_midpoint_price.has_value()
               && flow.last_midpoint_price.has_value()
               && *flow.first_midpoint_price > 0.0) {
        flow.price_change_basis_points =
            (*flow.last_midpoint_price / *flow.first_midpoint_price - 1.0)
            * 10'000.0;
    }
    if (flow.price_change_basis_points.has_value()
        && flow.delta_ratio_percent.has_value()
        && std::abs(*flow.delta_ratio_percent) > 1e-9) {
        flow.impact_efficiency = *flow.price_change_basis_points
            / std::abs(*flow.delta_ratio_percent);
    }
    result.complete = trade_history_reaches_start && quote_history_reaches_start;
    return result;
}

} // namespace daytrader::analysis
