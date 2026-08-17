#pragma once

#include "daytrader/domain/InstrumentBars.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace daytrader::market_data {

// Builds a validated symbol index over one immutable callback snapshot.
class InstrumentBarsLookup {
public:
    explicit InstrumentBarsLookup(const std::vector<domain::InstrumentBars>& instruments);

    // Required lookup throws when core signal data is absent.
    [[nodiscard]] const domain::InstrumentBars& at(const std::string& symbol) const;
    // Optional lookup supports VIX and leveraged data that may be unavailable.
    [[nodiscard]] const domain::InstrumentBars* find(const std::string& symbol) const noexcept;

private:
    std::unordered_map<std::string, const domain::InstrumentBars*> by_symbol_;
};

} // namespace daytrader::market_data
