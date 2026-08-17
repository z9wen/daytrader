#include "daytrader/indicators/TechnicalIndicators.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace daytrader::indicators {

EmaState exponential_moving_average(std::span<const double> values, std::size_t period)
{
    if (values.size() < 2) {
        throw std::invalid_argument("EMA requires at least two values");
    }
    if (period == 0) {
        throw std::invalid_argument("EMA period must be positive");
    }

    const double alpha = 2.0 / (static_cast<double>(period) + 1.0);
    double ema = values.front();
    double previous = ema;
    for (std::size_t index = 1; index < values.size(); ++index) {
        previous = ema;
        ema = alpha * values[index] + (1.0 - alpha) * ema;
    }
    return EmaState{.current = ema, .previous = previous};
}

std::optional<double> session_vwap(
    std::span<const domain::MarketBar* const> bars,
    const time::TimeZoneFormatter& time_formatter
)
{
    if (bars.empty()) {
        return std::nullopt;
    }

    const std::string latest_session = time_formatter.format_date(bars.back()->epoch_seconds);
    double price_volume_sum{};
    double volume_sum{};

    for (auto bar = bars.rbegin(); bar != bars.rend(); ++bar) {
        const auto& current = **bar;
        if (time_formatter.format_date(current.epoch_seconds) != latest_session) {
            break;
        }
        if (!current.volume.has_value() || *current.volume <= 0.0) {
            continue;
        }

        const double price = current.weighted_average_price.value_or(
            (current.high + current.low + current.close) / 3.0
        );
        price_volume_sum += price * *current.volume;
        volume_sum += *current.volume;
    }

    if (volume_sum == 0.0) {
        return std::nullopt;
    }
    return price_volume_sum / volume_sum;
}

double average_true_range(
    std::span<const domain::MarketBar* const> bars,
    std::size_t period
)
{
    if (period == 0) {
        throw std::invalid_argument("ATR period must be positive");
    }
    if (bars.size() < period) {
        throw std::invalid_argument("ATR requires at least period bars");
    }

    std::vector<double> true_ranges;
    true_ranges.reserve(bars.size());
    true_ranges.push_back(bars.front()->high - bars.front()->low);
    for (std::size_t index = 1; index < bars.size(); ++index) {
        const auto& current = *bars[index];
        const double previous_close = bars[index - 1]->close;
        true_ranges.push_back(std::max({
            current.high - current.low,
            std::abs(current.high - previous_close),
            std::abs(current.low - previous_close),
        }));
    }

    double atr{};
    for (std::size_t index = 0; index < period; ++index) {
        atr += true_ranges[index];
    }
    atr /= static_cast<double>(period);
    for (std::size_t index = period; index < true_ranges.size(); ++index) {
        atr = (atr * static_cast<double>(period - 1) + true_ranges[index])
            / static_cast<double>(period);
    }
    return atr;
}

} // namespace daytrader::indicators
