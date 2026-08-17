#include "daytrader/market_data/BarSeriesAligner.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <unordered_map>

namespace daytrader::market_data {
namespace {

[[nodiscard]] std::unordered_map<std::int64_t, const domain::MarketBar*> index_bars(
    const std::vector<domain::MarketBar>& bars
)
{
    std::unordered_map<std::int64_t, const domain::MarketBar*> indexed;
    indexed.reserve(bars.size());
    for (const auto& bar : bars) {
        indexed.insert_or_assign(bar.epoch_seconds, &bar);
    }
    return indexed;
}

} // namespace

BarSeriesAligner::BarSeriesAligner(std::chrono::seconds bar_interval)
    : bar_interval_{bar_interval}
{
    if (bar_interval_ <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("bar interval must be positive");
    }
}

std::vector<AlignedBarPair> BarSeriesAligner::align_completed(
    const domain::InstrumentBars& signal,
    const domain::InstrumentBars& benchmark
) const
{
    const auto benchmark_by_time = index_bars(benchmark.bars);
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    std::vector<AlignedBarPair> aligned;
    aligned.reserve(std::min(signal.bars.size(), benchmark.bars.size()));
    for (const auto& signal_bar : signal.bars) {
        const auto benchmark_bar = benchmark_by_time.find(signal_bar.epoch_seconds);
        if (benchmark_bar == benchmark_by_time.end()) {
            continue;
        }
        if (signal_bar.epoch_seconds + bar_interval_.count() > now) {
            continue;
        }
        aligned.push_back(AlignedBarPair{
            .epoch_seconds = signal_bar.epoch_seconds,
            .signal = &signal_bar,
            .benchmark = benchmark_bar->second,
        });
    }

    std::ranges::sort(aligned, {}, &AlignedBarPair::epoch_seconds);
    return aligned;
}

} // namespace daytrader::market_data
