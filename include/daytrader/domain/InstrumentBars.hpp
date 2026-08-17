#pragma once

#include "daytrader/domain/MarketBar.hpp"

#include <string>
#include <vector>

namespace daytrader::domain {

// Chronologically ordered bars belonging to one requested instrument.
struct InstrumentBars {
    std::string symbol;
    std::vector<MarketBar> bars;
};

} // namespace daytrader::domain
