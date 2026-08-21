#include "daytrader/ibkr/IbkrErrorClassifier.hpp"
#include "daytrader/ibkr/HistoricalRequestPlanner.hpp"
#include "daytrader/market_data/BarSeriesAligner.hpp"
#include "daytrader/market_data/BarTimeframeTransformer.hpp"
#include "daytrader/market_data/CompletedBarSynchronizer.hpp"
#include "daytrader/market_data/InstrumentBarsLookup.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] daytrader::domain::MarketBar bar(std::int64_t epoch_seconds)
{
    return daytrader::domain::MarketBar{.epoch_seconds = epoch_seconds, .close = 100.0};
}

void aligner_keeps_only_matching_completed_timestamps()
{
    constexpr std::int64_t start = 1'700'000'000;
    const daytrader::domain::InstrumentBars signal{
        .symbol = "SIGNAL",
        .bars = {bar(start), bar(start + 300), bar(start + 600)},
    };
    const daytrader::domain::InstrumentBars benchmark{
        .symbol = "BENCHMARK",
        .bars = {bar(start), bar(start + 600)},
    };

    const auto aligned = daytrader::market_data::BarSeriesAligner{
        std::chrono::minutes{5}
    }.align_completed(signal, benchmark);
    require(aligned.size() == 2, "expected two matching timestamps");
    require(aligned.front().epoch_seconds == start, "expected chronological output");
    require(aligned.back().epoch_seconds == start + 600, "expected latest common bar");
}

void lookup_rejects_duplicate_symbols()
{
    const std::vector<daytrader::domain::InstrumentBars> duplicate{
        daytrader::domain::InstrumentBars{.symbol = "SPY"},
        daytrader::domain::InstrumentBars{.symbol = "SPY"},
    };

    bool threw{};
    try {
        static_cast<void>(daytrader::market_data::InstrumentBarsLookup{duplicate});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "expected duplicate symbols to be rejected");
}

void completed_bar_synchronizer_waits_for_every_instrument()
{
    constexpr std::int64_t start = 1'700'000'000;
    constexpr std::int64_t now = start + 1'000;
    const std::vector<daytrader::domain::InstrumentBars> instruments{
        daytrader::domain::InstrumentBars{
            .symbol = "SPY",
            .bars = {bar(start), bar(start + 300), bar(start + 600)},
        },
        daytrader::domain::InstrumentBars{
            .symbol = "QQQ",
            .bars = {bar(start), bar(start + 300)},
        },
        daytrader::domain::InstrumentBars{
            .symbol = "SOXX",
            .bars = {bar(start), bar(start + 300), bar(start + 600)},
        },
    };

    const auto completed = daytrader::market_data::latest_common_completed_bar(
        instruments,
        std::chrono::minutes{5},
        now
    );
    require(completed.has_value(), "expected a common completed bar");
    require(*completed == start + 300, "expected the latest timestamp shared by every ETF");
}

void one_minute_source_resamples_without_losing_market_fields()
{
    constexpr std::int64_t start = 1'704'205'800; // 2024-01-02 09:30 New York
    daytrader::domain::InstrumentBars source{.symbol = "SOXX"};
    for (int index = 0; index < 5; ++index) {
        const double open = 100.0 + index;
        source.bars.push_back(daytrader::domain::MarketBar{
            .epoch_seconds = start + index * 60,
            .open = open,
            .high = open + 2.0,
            .low = open - 1.0,
            .close = open + 1.0,
            .volume = 100.0 * (index + 1),
            .weighted_average_price = open + 0.5,
            .trade_count = 10 * (index + 1),
        });
    }

    const auto result = daytrader::market_data::resample_bars(
        {source},
        std::chrono::minutes{1},
        std::chrono::minutes{5}
    );
    require(result.size() == 1 && result[0].bars.size() == 1,
            "five one-minute bars should form one five-minute bar");
    const auto& aggregate = result[0].bars[0];
    require(aggregate.epoch_seconds == start, "aggregate timestamp mismatch");
    require(aggregate.open == 100.0 && aggregate.high == 106.0
                && aggregate.low == 99.0 && aggregate.close == 105.0,
            "aggregate OHLC mismatch");
    require(aggregate.volume == 1'500.0, "aggregate volume mismatch");
    require(aggregate.trade_count == 150, "aggregate trade count mismatch");
    require(aggregate.weighted_average_price.has_value()
                && std::abs(*aggregate.weighted_average_price - 103.1666666667) < 1e-9,
            "aggregate WAP must be volume weighted");
    require(source.bars.size() == 5,
            "resampling must not mutate or truncate the one-minute source");
}

void regular_session_is_a_view_and_keeps_extended_source()
{
    constexpr std::int64_t premarket = 1'704'204'000; // 09:00 New York
    constexpr std::int64_t open = 1'704'205'800;      // 09:30 New York
    constexpr std::int64_t close = 1'704'229'200;     // 16:00 New York
    const std::vector<daytrader::domain::InstrumentBars> source{
        daytrader::domain::InstrumentBars{
            .symbol = "QQQ",
            .bars = {bar(premarket), bar(open), bar(close)},
        },
    };
    const auto rth = daytrader::market_data::regular_session_view(
        source,
        "America/New_York"
    );
    require(rth[0].bars.size() == 1 && rth[0].bars[0].epoch_seconds == open,
            "RTH view should contain only regular-session bars");
    require(source[0].bars.size() == 3,
            "RTH analysis must not delete premarket or after-hours source bars");
}

void retries_only_after_an_actual_pacing_response()
{
    require(
        daytrader::ibkr::is_pacing_or_rate_limit_error(
            "Historical data request pacing violation"
        ),
        "an actual pacing response should activate retry"
    );
    require(
        daytrader::ibkr::is_pacing_or_rate_limit_error(
            "Max rate of messages per second has been exceeded"
        ),
        "an actual max-rate response should activate retry"
    );
    require(
        !daytrader::ibkr::is_pacing_or_rate_limit_error(
            "Market data farm connection is OK"
        ),
        "normal IBKR responses must never trigger local throttling"
    );
    require(
        daytrader::ibkr::is_pacing_or_rate_limit_error(
            100,
            "Max rate of messages per second has been exceeded"
        ),
        "official error 100 should activate reactive retry"
    );
    require(
        daytrader::ibkr::is_market_data_capacity_error(101),
        "official error 101 should be identified as ticker-line capacity"
    );
    require(
        !daytrader::ibkr::is_market_data_capacity_error(100),
        "message pacing and ticker-line capacity are different responses"
    );
    require(
        daytrader::ibkr::is_historical_no_data_error(
            162,
            "HMDS query returned no data"
        ),
        "an empty response should remain distinguishable for schedule validation"
    );
    require(
        !daytrader::ibkr::is_historical_no_data_error(
            162,
            "Historical data request pacing violation"
        ),
        "a pacing response must not be mistaken for an empty date"
    );
}

void plans_one_minute_history_at_the_official_duration_boundary()
{
    const auto ytd = daytrader::ibkr::plan_one_minute_day_windows(240);
    require(ytd.size() == 1 && ytd[0].duration_days == 240
                && ytd[0].end_delay_days == 0,
            "a normal YTD request should not be split into artificial pages");

    const auto longer = daytrader::ibkr::plan_one_minute_day_windows(500);
    require(longer.size() == 2,
            "ranges beyond the documented 365-day maximum should be split");
    require(longer[0].duration_days == 135 && longer[0].end_delay_days == 365,
            "the oldest partial request window is incorrect");
    require(longer[1].duration_days == 365 && longer[1].end_delay_days == 0,
            "the newest request must use the documented full duration");

    const auto durable = daytrader::ibkr::plan_one_minute_day_windows(75, 30);
    require(durable.size() == 3,
            "a durable request plan should preserve the complete range");
    require(durable[0].duration_days == 15 && durable[0].end_delay_days == 60,
            "the oldest durable window should contain the remainder");
    require(durable[1].duration_days == 30 && durable[1].end_delay_days == 30,
            "the middle durable window should remain contiguous");
    require(durable[2].duration_days == 30 && durable[2].end_delay_days == 0,
            "the newest durable window should end at the present");
}

void plans_bulk_cache_years_from_newest_to_oldest()
{
    using namespace std::chrono;
    const auto first = sys_days{year{2025} / December / day{30}};
    const auto last = sys_days{year{2026} / January / day{5}};
    const auto days = daytrader::ibkr::plan_market_weekdays_newest_first(first, last);
    require(days.size() == 5, "weekends should not create historical requests");
    require(days.front() == last, "the newest year must be planned first");
    require(days.back() == first, "the oldest requested date must remain included");
    require(
        year_month_day{days[3]}.year() == year{2025},
        "the plan should cross into the prior year only after newer weekdays"
    );
}

void recognizes_connection_interruptions_without_hiding_request_errors()
{
    require(
        daytrader::ibkr::is_connection_interruption_error(
            "Couldn't connect to TWS"
        ),
        "a failed TWS connection should be retried"
    );
    require(
        daytrader::ibkr::is_connection_interruption_error(
            "IBKR connection closed while fetching historical data"
        ),
        "an interrupted backfill should be resumed"
    );
    require(
        daytrader::ibkr::is_connection_interruption_error(
            "HMDS server disconnect occurred. Attempting reconnection..."
        ),
        "an explicit historical-data farm interruption should be retried"
    );
    require(
        !daytrader::ibkr::is_connection_interruption_error(
            "No security definition has been found"
        ),
        "an explicit request error must not be mistaken for a disconnect"
    );
}

} // namespace

int main()
{
    try {
        aligner_keeps_only_matching_completed_timestamps();
        lookup_rejects_duplicate_symbols();
        completed_bar_synchronizer_waits_for_every_instrument();
        one_minute_source_resamples_without_losing_market_fields();
        regular_session_is_a_view_and_keeps_extended_source();
        retries_only_after_an_actual_pacing_response();
        plans_one_minute_history_at_the_official_duration_boundary();
        plans_bulk_cache_years_from_newest_to_oldest();
        recognizes_connection_interruptions_without_hiding_request_errors();
        std::cout << "MarketDataTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "MarketDataTests failed: " << exception.what() << '\n';
        return 1;
    }
}
