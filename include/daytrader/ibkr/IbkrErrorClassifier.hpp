#pragma once

#include <string_view>

namespace daytrader::ibkr {

// Detects a pacing/rate response after TWS has actually returned it. This is
// intentionally not used to throttle, cap, or delay requests proactively.
[[nodiscard]] bool is_pacing_or_rate_limit_error(std::string_view message);
[[nodiscard]] bool is_pacing_or_rate_limit_error(
    int error_code,
    std::string_view message
);

// Error 101 is IBKR's explicit market-data-line capacity response. It is not
// treated as pacing because reconnecting cannot create more subscribed lines.
[[nodiscard]] bool is_market_data_capacity_error(int error_code);

// Identifies an empty historical response without guessing why it is empty.
// The schedule-aware caller decides whether the date is closed or must remain
// pending; an empty response is never sufficient evidence of a holiday.
[[nodiscard]] bool is_historical_no_data_error(
    int error_code,
    std::string_view message
);

// Connection interruptions are retried because they say nothing about whether
// the requested data is valid. They must not leave a background backfill only
// partially populated.
[[nodiscard]] bool is_connection_interruption_error(std::string_view message);

} // namespace daytrader::ibkr
