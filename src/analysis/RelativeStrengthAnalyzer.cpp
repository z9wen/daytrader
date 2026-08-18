#include "daytrader/analysis/RelativeStrengthAnalyzer.hpp"

#include <cstddef>
#include <optional>
#include <stdexcept>

namespace daytrader::analysis {
namespace {

[[nodiscard]] std::optional<double> horizon_change(
    std::span<const market_data::AlignedBarPair> pairs,
    std::size_t lookback
)
{
    if (pairs.size() <= lookback) {
        return std::nullopt;
    }
    const auto& current = pairs.back();
    const auto& prior = pairs[pairs.size() - 1 - lookback];
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
        .fifteen_minute_percent = horizon_change(signal_vs_benchmark, 3),
        .thirty_minute_percent = horizon_change(signal_vs_benchmark, 6),
        .sixty_minute_percent = horizon_change(signal_vs_benchmark, 12),
    };
}

} // namespace daytrader::analysis
