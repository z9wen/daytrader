#pragma once

#include "daytrader/domain/MarketBar.hpp"
#include "daytrader/time/TimeZoneFormatter.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace daytrader::indicators {

// Current and prior EMA values allow callers to measure the latest slope.
struct EmaState {
    double current{};
    double previous{};
};

// Seeds with a simple average and then applies the standard EMA recurrence.
[[nodiscard]] EmaState exponential_moving_average(
    std::span<const double> values,
    std::size_t period
);

// Calculates regular-session VWAP from IBKR weighted price and volume fields.
[[nodiscard]] std::optional<double> session_vwap(
    std::span<const domain::MarketBar* const> bars,
    const time::TimeZoneFormatter& time_formatter
);

// Wilder-smoothed ATR calculated from completed OHLC bars.
[[nodiscard]] double average_true_range(
    std::span<const domain::MarketBar* const> bars,
    std::size_t period
);

} // namespace daytrader::indicators
