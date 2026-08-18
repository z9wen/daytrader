#pragma once

#include "daytrader/domain/LiveTradeContext.hpp"
#include "daytrader/domain/MarketScan.hpp"

namespace daytrader::analysis {

// Adds ATR-normalized Order Flow and recalculates live leveraged execution
// guidance without coupling the IBKR socket layer to strategy state.
class LiveTradeContextEnricher {
public:
    [[nodiscard]] domain::MarketScan enrich(
        domain::MarketScan scan,
        domain::LiveTradeContext context
    ) const;
};

} // namespace daytrader::analysis
