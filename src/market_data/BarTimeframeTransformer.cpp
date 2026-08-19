#include "daytrader/market_data/BarTimeframeTransformer.hpp"

#include "daytrader/time/TimeZoneFormatter.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace daytrader::market_data {
namespace {

constexpr int regular_session_start_minute = 9 * 60 + 30;
constexpr int regular_session_end_minute = 16 * 60;

[[nodiscard]] std::int64_t bucket_start(
    std::int64_t epoch_seconds,
    std::int64_t interval_seconds
)
{
    return epoch_seconds - epoch_seconds % interval_seconds;
}

void append_to_bucket(
    domain::MarketBar& aggregate,
    const domain::MarketBar& bar,
    double& price_volume_sum,
    double& volume_sum,
    int& trade_count_sum,
    bool& has_trade_count
)
{
    aggregate.high = std::max(aggregate.high, bar.high);
    aggregate.low = std::min(aggregate.low, bar.low);
    aggregate.close = bar.close;
    if (bar.volume.has_value() && *bar.volume > 0.0) {
        const double price = bar.weighted_average_price.value_or(
            (bar.high + bar.low + bar.close) / 3.0
        );
        price_volume_sum += price * *bar.volume;
        volume_sum += *bar.volume;
    }
    if (bar.trade_count.has_value()) {
        trade_count_sum += *bar.trade_count;
        has_trade_count = true;
    }
}

} // namespace

std::vector<domain::InstrumentBars> regular_session_view(
    const std::vector<domain::InstrumentBars>& instruments,
    const std::string& time_zone
)
{
    const time::TimeZoneFormatter formatter{time_zone};
    std::vector<domain::InstrumentBars> result;
    result.reserve(instruments.size());
    for (const auto& instrument : instruments) {
        domain::InstrumentBars filtered{.symbol = instrument.symbol};
        filtered.bars.reserve(instrument.bars.size());
        for (const auto& bar : instrument.bars) {
            const int minute = formatter.minutes_since_midnight(bar.epoch_seconds);
            if (minute >= regular_session_start_minute
                && minute < regular_session_end_minute) {
                filtered.bars.push_back(bar);
            }
        }
        result.push_back(std::move(filtered));
    }
    return result;
}

std::vector<domain::InstrumentBars> resample_bars(
    const std::vector<domain::InstrumentBars>& instruments,
    std::chrono::seconds source_interval,
    std::chrono::seconds target_interval
)
{
    if (source_interval <= std::chrono::seconds::zero()
        || target_interval <= source_interval
        || target_interval.count() % source_interval.count() != 0) {
        throw std::invalid_argument(
            "target bar interval must be an exact multiple of the source interval"
        );
    }

    std::vector<domain::InstrumentBars> result;
    result.reserve(instruments.size());
    for (const auto& instrument : instruments) {
        domain::InstrumentBars resampled{.symbol = instrument.symbol};
        std::optional<domain::MarketBar> aggregate;
        double price_volume_sum{};
        double volume_sum{};
        int trade_count_sum{};
        bool has_trade_count{};

        const auto finish_bucket = [&] {
            if (!aggregate.has_value()) {
                return;
            }
            aggregate->volume = volume_sum > 0.0
                ? std::optional<double>{volume_sum}
                : std::nullopt;
            aggregate->weighted_average_price = volume_sum > 0.0
                ? std::optional<double>{price_volume_sum / volume_sum}
                : std::nullopt;
            aggregate->trade_count = has_trade_count
                ? std::optional<int>{trade_count_sum}
                : std::nullopt;
            resampled.bars.push_back(*aggregate);
        };

        for (const auto& bar : instrument.bars) {
            const auto timestamp = bucket_start(
                bar.epoch_seconds,
                target_interval.count()
            );
            if (!aggregate.has_value() || aggregate->epoch_seconds != timestamp) {
                finish_bucket();
                aggregate = domain::MarketBar{
                    .epoch_seconds = timestamp,
                    .open = bar.open,
                    .high = bar.high,
                    .low = bar.low,
                    .close = bar.close,
                };
                price_volume_sum = 0.0;
                volume_sum = 0.0;
                trade_count_sum = 0;
                has_trade_count = false;
            }
            append_to_bucket(
                *aggregate,
                bar,
                price_volume_sum,
                volume_sum,
                trade_count_sum,
                has_trade_count
            );
        }
        finish_bucket();
        result.push_back(std::move(resampled));
    }
    return result;
}

} // namespace daytrader::market_data
