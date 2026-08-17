#include "daytrader/analysis/EntryZoneCalculator.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(double actual, double expected, const std::string& message)
{
    if (std::abs(actual - expected) > 1e-9) {
        throw std::runtime_error(message);
    }
}

void entry_zone_is_centered_on_vwap_and_scaled_by_atr()
{
    constexpr std::int64_t start = 1'700'000'000;
    std::array<daytrader::domain::MarketBar, 21> bars;
    std::array<const daytrader::domain::MarketBar*, 21> pointers;
    for (std::size_t index = 0; index < bars.size(); ++index) {
        const double close = 100.0 + static_cast<double>(index) * 0.01;
        bars[index] = daytrader::domain::MarketBar{
            .epoch_seconds = start + static_cast<std::int64_t>(index * 300),
            .open = close,
            .high = close + 1.0,
            .low = close - 1.0,
            .close = close,
            .volume = 100.0,
            .weighted_average_price = 100.0,
        };
        pointers[index] = &bars[index];
    }

    const auto zone = daytrader::analysis::EntryZoneCalculator{}.calculate(
        "SOXL",
        pointers,
        daytrader::time::TimeZoneFormatter{"America/New_York"}
    );
    require(zone.has_value(), "expected an entry zone");
    require(zone->symbol == "SOXL", "entry zone should retain its instrument symbol");
    require_near(zone->atr14, 2.0, "expected ATR14 from two-point ranges");
    require_near(zone->lower_price, 99.5, "unexpected lower entry price");
    require_near(zone->upper_price, 100.5, "unexpected upper entry price");
    require(
        zone->state == daytrader::domain::EntryZoneState::in_zone,
        "expected current price inside the entry zone"
    );
}

} // namespace

int main()
{
    try {
        entry_zone_is_centered_on_vwap_and_scaled_by_atr();
        std::cout << "EntryZoneTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "EntryZoneTests failed: " << exception.what() << '\n';
        return 1;
    }
}
