#include "daytrader/analysis/RotationGrouper.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using daytrader::domain::RankedEtf;
using daytrader::domain::RelativeStrengthSignal;

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] RankedEtf rank(
    std::string symbol,
    RelativeStrengthSignal signal,
    double relative_strength
)
{
    return RankedEtf{
        .symbol = std::move(symbol),
        .relative_change_60_min_percent = relative_strength,
        .signal = signal,
    };
}

void groups_rotate_and_sort_by_signal_strength()
{
    const std::vector<RankedEtf> rankings{
        rank("WEAK_A", RelativeStrengthSignal::weak, -0.2),
        rank("STRONG_B", RelativeStrengthSignal::strong, 0.4),
        rank("NEUTRAL_A", RelativeStrengthSignal::neutral, 0.1),
        rank("STRONG_A", RelativeStrengthSignal::strong, 0.8),
        rank("WEAK_B", RelativeStrengthSignal::weak, -0.7),
    };

    const auto groups = daytrader::analysis::RotationGrouper{}.group(rankings);
    require(groups.strong.size() == 2, "expected two strong industries");
    require(groups.neutral.size() == 1, "expected one neutral industry");
    require(groups.weak.size() == 2, "expected two weak industries");
    require(groups.strong.front()->symbol == "STRONG_A", "strongest should be first");
    require(groups.neutral.front()->symbol == "NEUTRAL_A", "neutral group mismatch");
    require(groups.weak.front()->symbol == "WEAK_B", "weakest should be first");
}

} // namespace

int main()
{
    try {
        groups_rotate_and_sort_by_signal_strength();
        std::cout << "IndustryRotationTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "IndustryRotationTests failed: " << exception.what() << '\n';
        return 1;
    }
}
