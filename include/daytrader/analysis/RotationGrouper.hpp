#pragma once

#include "daytrader/domain/MarketScan.hpp"

#include <span>
#include <vector>

namespace daytrader::analysis {

// Non-owning views into one ranking collection, split for presentation.
struct RotationGroups {
    std::vector<const domain::RankedEtf*> strong;
    std::vector<const domain::RankedEtf*> neutral;
    std::vector<const domain::RankedEtf*> weak;
};

// Groups either sector or industry rankings without duplicating sorting logic.
class RotationGrouper {
public:
    [[nodiscard]] RotationGroups group(
        std::span<const domain::RankedEtf> rankings
    ) const;
};

} // namespace daytrader::analysis
