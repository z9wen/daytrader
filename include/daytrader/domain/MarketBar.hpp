#pragma once

#include <cstdint>
#include <optional>

namespace daytrader::domain {

// Normalized OHLC bar. Optional fields accommodate indices that report no volume.
struct MarketBar {
    std::int64_t epoch_seconds{};
    double open{};
    double high{};
    double low{};
    double close{};
    std::optional<double> volume;
    std::optional<double> weighted_average_price;
    std::optional<int> trade_count;
};

} // namespace daytrader::domain
