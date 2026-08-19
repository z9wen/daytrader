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

// Calculates the current extended-hours segment VWAP. During RTH the completed
// premarket anchor remains visible; after 16:00 a fresh after-hours anchor starts.
[[nodiscard]] std::optional<double> extended_session_vwap(
    std::span<const domain::MarketBar* const> bars,
    const time::TimeZoneFormatter& time_formatter
);

// Calculates the independently anchored 09:30-16:00 New York VWAP.
[[nodiscard]] std::optional<double> regular_session_vwap(
    std::span<const domain::MarketBar* const> bars,
    const time::TimeZoneFormatter& time_formatter
);

// Chooses RTH VWAP during regular trading and EXT VWAP outside RTH. Existing
// callers retain one active reference while snapshots expose both anchors.
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
