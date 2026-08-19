#include "daytrader/indicators/TechnicalIndicators.hpp"

#include <array>
#include <cmath>
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

void ema_returns_current_and_previous_values()
{
    constexpr std::array values{1.0, 2.0, 3.0};
    const auto ema = daytrader::indicators::exponential_moving_average(values, 2);
    require_near(ema.previous, 5.0 / 3.0, "unexpected previous EMA");
    require_near(ema.current, 23.0 / 9.0, "unexpected current EMA");
}

void vwap_uses_only_the_latest_new_york_session()
{
    using daytrader::domain::MarketBar;
    const MarketBar previous_day{
        .epoch_seconds = 1'699'913'600,
        .volume = 100.0,
        .weighted_average_price = 50.0,
    };
    const MarketBar first{
        .epoch_seconds = 1'700'000'000,
        .volume = 1.0,
        .weighted_average_price = 100.0,
    };
    const MarketBar second{
        .epoch_seconds = 1'700'000'300,
        .volume = 3.0,
        .weighted_average_price = 110.0,
    };
    const std::array<const MarketBar*, 3> bars{&previous_day, &first, &second};

    const auto value = daytrader::indicators::session_vwap(
        bars,
        daytrader::time::TimeZoneFormatter{"America/New_York"}
    );
    require(value.has_value(), "expected a session VWAP");
    require_near(*value, 107.5, "VWAP should exclude the previous session");
}

void extended_and_regular_vwap_keep_independent_anchors()
{
    using daytrader::domain::MarketBar;
    const MarketBar premarket{
        .epoch_seconds = 1'704'200'400, // 08:00 America/New_York
        .volume = 100.0,
        .weighted_average_price = 90.0,
    };
    const MarketBar open{
        .epoch_seconds = 1'704'205'800, // 09:30 America/New_York
        .volume = 100.0,
        .weighted_average_price = 100.0,
    };
    const MarketBar next{
        .epoch_seconds = 1'704'205'860,
        .volume = 300.0,
        .weighted_average_price = 110.0,
    };
    const std::array<const MarketBar*, 3> bars{&premarket, &open, &next};
    const daytrader::time::TimeZoneFormatter formatter{"America/New_York"};

    const auto extended = daytrader::indicators::extended_session_vwap(
        bars,
        formatter
    );
    const auto regular = daytrader::indicators::regular_session_vwap(
        bars,
        formatter
    );
    const auto active = daytrader::indicators::session_vwap(bars, formatter);
    require(extended.has_value() && regular.has_value() && active.has_value(),
            "expected both independently anchored VWAPs");
    require_near(*extended, 90.0,
                 "EXT VWAP should keep the premarket segment independent");
    require_near(*regular, 107.5, "RTH VWAP should begin at 09:30");
    require_near(*active, *regular, "RTH should be the active intraday VWAP");

    const MarketBar after_hours{
        .epoch_seconds = 1'704'229'200, // 16:00 America/New_York
        .volume = 200.0,
        .weighted_average_price = 112.0,
    };
    const std::array<const MarketBar*, 4> after_hours_bars{
        &premarket,
        &open,
        &next,
        &after_hours,
    };
    const auto after_hours_extended = daytrader::indicators::extended_session_vwap(
        after_hours_bars,
        formatter
    );
    const auto after_hours_active = daytrader::indicators::session_vwap(
        after_hours_bars,
        formatter
    );
    require(after_hours_extended.has_value() && after_hours_active.has_value(),
            "after-hours VWAP should be available");
    require_near(*after_hours_extended, 112.0,
                 "after-hours VWAP should restart independently at 16:00");
    require_near(*after_hours_active, *after_hours_extended,
                 "EXT should be the active VWAP after 16:00");
}

void atr_uses_true_ranges_and_wilder_initial_average()
{
    using daytrader::domain::MarketBar;
    const MarketBar first{.high = 11.0, .low = 9.0, .close = 10.0};
    const MarketBar second{.high = 13.0, .low = 10.0, .close = 12.0};
    const MarketBar third{.high = 14.0, .low = 11.0, .close = 13.0};
    const std::array<const MarketBar*, 3> bars{&first, &second, &third};

    const auto atr = daytrader::indicators::average_true_range(bars, 3);
    require_near(atr, 8.0 / 3.0, "unexpected ATR");
}

} // namespace

int main()
{
    try {
        ema_returns_current_and_previous_values();
        vwap_uses_only_the_latest_new_york_session();
        extended_and_regular_vwap_keep_independent_anchors();
        atr_uses_true_ranges_and_wilder_initial_average();
        std::cout << "TechnicalIndicatorsTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "TechnicalIndicatorsTests failed: " << exception.what() << '\n';
        return 1;
    }
}
