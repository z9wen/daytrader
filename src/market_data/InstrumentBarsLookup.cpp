#include "daytrader/market_data/InstrumentBarsLookup.hpp"

#include <stdexcept>

namespace daytrader::market_data {

InstrumentBarsLookup::InstrumentBarsLookup(
    const std::vector<domain::InstrumentBars>& instruments
)
{
    by_symbol_.reserve(instruments.size());
    for (const auto& instrument : instruments) {
        if (!by_symbol_.emplace(instrument.symbol, &instrument).second) {
            throw std::invalid_argument("duplicate instrument bars: " + instrument.symbol);
        }
    }
}

const domain::InstrumentBars& InstrumentBarsLookup::at(const std::string& symbol) const
{
    const auto found = by_symbol_.find(symbol);
    if (found == by_symbol_.end()) {
        throw std::invalid_argument("market scan requires " + symbol + " bars");
    }
    return *found->second;
}

const domain::InstrumentBars* InstrumentBarsLookup::find(const std::string& symbol) const noexcept
{
    const auto found = by_symbol_.find(symbol);
    return found == by_symbol_.end() ? nullptr : found->second;
}

} // namespace daytrader::market_data
