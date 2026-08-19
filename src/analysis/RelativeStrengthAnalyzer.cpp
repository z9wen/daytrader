#include "daytrader/analysis/RelativeStrengthAnalyzer.hpp"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <optional>
#include <stdexcept>

namespace daytrader::analysis {
namespace {

[[nodiscard]] std::optional<double> horizon_change(
    std::span<const market_data::AlignedBarPair> pairs,
    std::chrono::minutes horizon
)
{
    if (pairs.size() < 2) {
        return std::nullopt;
    }
    const auto& current = pairs.back();
    const auto target = current.epoch_seconds - horizon.count() * 60;
    const auto after_target = std::ranges::upper_bound(
        pairs,
        target,
        {},
        &market_data::AlignedBarPair::epoch_seconds
    );
    if (after_target == pairs.begin()) {
        return std::nullopt;
    }
    const auto& prior = *std::prev(after_target);
    if (current.benchmark->close <= 0.0 || prior.benchmark->close <= 0.0
        || prior.signal->close <= 0.0) {
        throw std::runtime_error("relative strength requires positive close prices");
    }
    const double current_ratio = current.signal->close / current.benchmark->close;
    const double prior_ratio = prior.signal->close / prior.benchmark->close;
    return (current_ratio / prior_ratio - 1.0) * 100.0;
}

} // namespace

domain::RelativeStrengthHorizons RelativeStrengthAnalyzer::analyze(
    std::span<const market_data::AlignedBarPair> signal_vs_benchmark
) const
{
    return domain::RelativeStrengthHorizons{
        .fifteen_minute_percent = horizon_change(
            signal_vs_benchmark,
            std::chrono::minutes{15}
        ),
        .thirty_minute_percent = horizon_change(
            signal_vs_benchmark,
            std::chrono::minutes{30}
        ),
        .sixty_minute_percent = horizon_change(
            signal_vs_benchmark,
            std::chrono::minutes{60}
        ),
    };
}

} // namespace daytrader::analysis
