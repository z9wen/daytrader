#include "daytrader/analysis/OrderFlowWindowAnalyzer.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace daytrader::analysis {

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
    bool trade_history_reaches_start{};
    bool quote_history_reaches_start{};
    std::optional<double> first_trade_price;
    std::optional<double> last_trade_price;

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
        if (quote.epoch_seconds < start_timestamp || quote.epoch_seconds > end_timestamp) {
            continue;
        }
        const double displayed_size = quote.bid_size + quote.ask_size;
        if (!std::isfinite(displayed_size) || displayed_size <= 0.0) {
            continue;
        }
        quote_imbalance_sum +=
            (quote.bid_size - quote.ask_size) / displayed_size * 100.0;
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
    }
    if (first_trade_price.has_value() && last_trade_price.has_value()
        && *first_trade_price > 0.0) {
        flow.first_trade_price = first_trade_price;
        flow.last_trade_price = last_trade_price;
        flow.price_change_basis_points =
            (*last_trade_price / *first_trade_price - 1.0) * 10'000.0;
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
