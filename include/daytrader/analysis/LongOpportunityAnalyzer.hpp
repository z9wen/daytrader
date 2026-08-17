#pragma once

#include "daytrader/domain/MarketScan.hpp"

namespace daytrader::analysis {

// Converts transparent trend inputs into separate entry and holding guidance.
// It deliberately does not know about positions or submit orders, which makes
// the exact same decision logic reusable by live scans and historical tests.
class LongOpportunityAnalyzer {
public:
    [[nodiscard]] domain::LongOpportunity analyze(
        const domain::RankedEtf& rank,
        domain::MarketRegime market_regime
    ) const;
};

} // namespace daytrader::analysis
