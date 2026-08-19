#pragma once

#include "daytrader/domain/InstrumentBars.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace daytrader::market_data {

// Builds an analysis-only RTH view. The source collection is left untouched so
// extended-hours bars remain available in the durable one-minute cache.
[[nodiscard]] std::vector<domain::InstrumentBars> regular_session_view(
    const std::vector<domain::InstrumentBars>& instruments,
    const std::string& time_zone
);

// Aggregates a smaller completed-bar series into a larger timeframe without
// discarding the source data. OHLC, volume, WAP and trade counts are preserved.
[[nodiscard]] std::vector<domain::InstrumentBars> resample_bars(
    const std::vector<domain::InstrumentBars>& instruments,
    std::chrono::seconds source_interval,
    std::chrono::seconds target_interval
);

} // namespace daytrader::market_data
