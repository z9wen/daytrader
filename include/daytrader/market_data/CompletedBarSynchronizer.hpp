#pragma once

#include "daytrader/domain/InstrumentBars.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>

namespace daytrader::market_data {

// Finds the newest timestamp completed and present across all instrument series.
[[nodiscard]] std::optional<std::int64_t> latest_common_completed_bar(
    std::span<const domain::InstrumentBars> instruments,
    std::chrono::seconds bar_interval,
    std::int64_t now_epoch_seconds
);

// Pointer overload avoids copying and lets callers synchronize a selected subset.
[[nodiscard]] std::optional<std::int64_t> latest_common_completed_bar(
    std::span<const domain::InstrumentBars* const> instruments,
    std::chrono::seconds bar_interval,
    std::int64_t now_epoch_seconds
);

} // namespace daytrader::market_data
