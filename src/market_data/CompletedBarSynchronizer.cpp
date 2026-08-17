#include "daytrader/market_data/CompletedBarSynchronizer.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace daytrader::market_data {

std::optional<std::int64_t> latest_common_completed_bar(
    std::span<const domain::InstrumentBars> instruments,
    std::chrono::seconds bar_interval,
    std::int64_t now_epoch_seconds
)
{
    std::vector<const domain::InstrumentBars*> pointers;
    pointers.reserve(instruments.size());
    for (const auto& instrument : instruments) {
        pointers.push_back(&instrument);
    }
    return latest_common_completed_bar(pointers, bar_interval, now_epoch_seconds);
}

std::optional<std::int64_t> latest_common_completed_bar(
    std::span<const domain::InstrumentBars* const> instruments,
    std::chrono::seconds bar_interval,
    std::int64_t now_epoch_seconds
)
{
    if (bar_interval <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("bar interval must be positive");
    }
    if (instruments.empty()) {
        return std::nullopt;
    }

    std::unordered_set<std::int64_t> common;
    for (const auto& bar : instruments.front()->bars) {
        if (bar.epoch_seconds + bar_interval.count() <= now_epoch_seconds) {
            common.insert(bar.epoch_seconds);
        }
    }

    for (const auto* instrument : instruments.subspan(1)) {
        std::unordered_set<std::int64_t> available;
        available.reserve(instrument->bars.size());
        for (const auto& bar : instrument->bars) {
            if (bar.epoch_seconds + bar_interval.count() <= now_epoch_seconds) {
                available.insert(bar.epoch_seconds);
            }
        }

        std::erase_if(common, [&available](std::int64_t timestamp) {
            return !available.contains(timestamp);
        });
        if (common.empty()) {
            return std::nullopt;
        }
    }

    return *std::ranges::max_element(common);
}

} // namespace daytrader::market_data
