#include "daytrader/analysis/RotationGrouper.hpp"

#include <algorithm>
#include <functional>

namespace daytrader::analysis {
namespace {

void sort_descending(std::vector<const domain::RankedEtf*>& ranks)
{
    std::ranges::sort(
        ranks,
        std::greater{},
        &domain::RankedEtf::relative_change_60_min_percent
    );
}

} // namespace

RotationGroups RotationGrouper::group(std::span<const domain::RankedEtf> rankings) const
{
    RotationGroups groups;
    for (const auto& rank : rankings) {
        switch (rank.signal) {
        case domain::RelativeStrengthSignal::strong:
            groups.strong.push_back(&rank);
            break;
        case domain::RelativeStrengthSignal::neutral:
            groups.neutral.push_back(&rank);
            break;
        case domain::RelativeStrengthSignal::weak:
            groups.weak.push_back(&rank);
            break;
        }
    }

    sort_descending(groups.strong);
    sort_descending(groups.neutral);
    std::ranges::sort(
        groups.weak,
        std::less{},
        &domain::RankedEtf::relative_change_60_min_percent
    );
    return groups;
}

} // namespace daytrader::analysis
