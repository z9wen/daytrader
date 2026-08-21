#include "daytrader/ibkr/HistoricalRequestPlanner.hpp"

#include <algorithm>
#include <stdexcept>

namespace daytrader::ibkr {

std::vector<HistoricalDayWindow> plan_one_minute_day_windows(
    int calendar_days,
    int window_days
)
{
    if (calendar_days <= 0) {
        throw std::invalid_argument("historical calendar days must be positive");
    }
    if (window_days <= 0 || window_days > one_minute_max_duration_days) {
        throw std::invalid_argument(
            "historical window days must be within the IBKR duration boundary"
        );
    }

    std::vector<HistoricalDayWindow> windows;
    const int oldest_delay = ((calendar_days - 1) / window_days) * window_days;
    for (int end_delay = oldest_delay; end_delay >= 0;
         end_delay -= window_days) {
        windows.push_back(HistoricalDayWindow{
            .duration_days = std::min(
                window_days,
                calendar_days - end_delay
            ),
            .end_delay_days = end_delay,
        });
    }
    return windows;
}

std::vector<std::chrono::sys_days> plan_market_weekdays_newest_first(
    std::chrono::sys_days first_day,
    std::chrono::sys_days last_day
)
{
    if (first_day > last_day) {
        throw std::invalid_argument("historical first day must not follow last day");
    }

    std::vector<std::chrono::sys_days> days;
    days.reserve(static_cast<std::size_t>((last_day - first_day).count() + 1));
    for (auto day = last_day;; day -= std::chrono::days{1}) {
        const std::chrono::weekday weekday{day};
        if (weekday != std::chrono::Saturday && weekday != std::chrono::Sunday) {
            days.push_back(day);
        }
        if (day == first_day) {
            break;
        }
    }
    return days;
}

} // namespace daytrader::ibkr
