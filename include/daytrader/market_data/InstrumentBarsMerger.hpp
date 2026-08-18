#pragma once

#include "daytrader/domain/InstrumentBars.hpp"

#include <span>
#include <vector>

namespace daytrader::market_data {

// Shared cache/live-data merge primitives. Timestamp normalization makes the
// operation idempotent across reconnects and repeated IBKR refreshes.
void merge_instrument_bars(
    std::vector<domain::InstrumentBars>& destination,
    std::span<const domain::InstrumentBars> incoming
);

void merge_instrument_bars(
    std::vector<domain::InstrumentBars>& destination,
    std::vector<domain::InstrumentBars>&& incoming
);

void sort_and_deduplicate_bars(
    std::vector<domain::InstrumentBars>& instruments
);

} // namespace daytrader::market_data
