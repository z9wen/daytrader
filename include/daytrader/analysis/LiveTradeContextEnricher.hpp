#pragma once

#include "daytrader/domain/LiveTradeContext.hpp"
#include "daytrader/domain/MarketScan.hpp"

namespace daytrader::analysis {

// Adds ATR-normalized interpretation to raw live Order Flow without coupling
// the IBKR socket layer to scanner strategy state.
class LiveTradeContextEnricher {
public:
    [[nodiscard]] domain::LiveTradeContext enrich(
        const domain::MarketScan& scan,
        domain::LiveTradeContext context
    ) const;
};

} // namespace daytrader::analysis
