#pragma once

#include "daytrader/domain/InstrumentBars.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

namespace daytrader::market_data {

// Pointer pair for two instruments observed at exactly the same completed timestamp.
struct AlignedBarPair {
    std::int64_t epoch_seconds{};
    const domain::MarketBar* signal{};
    const domain::MarketBar* benchmark{};
};

// Intersects two sorted bar series and excludes their still-forming final bars.
class BarSeriesAligner {
public:
    explicit BarSeriesAligner(std::chrono::seconds bar_interval);

    [[nodiscard]] std::vector<AlignedBarPair> align_completed(
        const domain::InstrumentBars& signal,
        const domain::InstrumentBars& benchmark
    ) const;

private:
    std::chrono::seconds bar_interval_;
};

} // namespace daytrader::market_data
