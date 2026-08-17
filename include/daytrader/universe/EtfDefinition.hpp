#pragma once

#include "daytrader/config/MarketDataSettings.hpp"

#include <string>

namespace daytrader::universe {

// Presentation and ranking layer for each signal ETF.
enum class EtfGroup {
    broad_market,
    sector,
    industry,
};

// Static mapping from a signal ETF to its benchmark and optional leverage tickers.
struct EtfDefinition {
    config::HistoricalDataSettings market_data;
    std::string name;
    EtfGroup group{EtfGroup::industry};
    std::string benchmark_symbol;
    std::string leveraged_long_symbol;
    // Bear/inverse tickers are displayed as references but are not subscribed.
    std::string leveraged_short_symbol;
};

} // namespace daytrader::universe
