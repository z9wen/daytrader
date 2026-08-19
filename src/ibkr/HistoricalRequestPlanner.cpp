#include "daytrader/ibkr/HistoricalRequestPlanner.hpp"

#include <algorithm>
#include <stdexcept>

namespace daytrader::ibkr {

std::vector<HistoricalDayWindow> plan_one_minute_day_windows(int calendar_days)
{
    if (calendar_days <= 0) {
        throw std::invalid_argument("historical calendar days must be positive");
    }

    std::vector<HistoricalDayWindow> windows;
    const int oldest_delay = ((calendar_days - 1)
        / one_minute_max_duration_days) * one_minute_max_duration_days;
    for (int end_delay = oldest_delay; end_delay >= 0;
         end_delay -= one_minute_max_duration_days) {
        windows.push_back(HistoricalDayWindow{
            .duration_days = std::min(
                one_minute_max_duration_days,
                calendar_days - end_delay
            ),
            .end_delay_days = end_delay,
        });
    }
    return windows;
}

} // namespace daytrader::ibkr
