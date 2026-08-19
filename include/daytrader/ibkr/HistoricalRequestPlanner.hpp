#pragma once

#include <vector>

namespace daytrader::ibkr {

// One backward-looking IBKR historical-data window. end_delay_days == 0
// means that the window ends now; larger values move the end into the past.
struct HistoricalDayWindow {
    int duration_days{};
    int end_delay_days{};
};

// IBKR's current duration table permits up to 365 D for one-minute bars.
// Longer user-selected ranges are split only at that documented boundary.
inline constexpr int one_minute_max_duration_days = 365;

[[nodiscard]] std::vector<HistoricalDayWindow>
plan_one_minute_day_windows(
    int calendar_days,
    int window_days = one_minute_max_duration_days
);

} // namespace daytrader::ibkr
