#pragma once

#include "daytrader/universe/EtfDefinition.hpp"

#include <span>
#include <string>
#include <vector>

namespace daytrader::universe {

// Two market benchmarks, eleven standard sectors, and twenty industries.
[[nodiscard]] std::vector<EtfDefinition> default_etf_universe();

// Produces one request for every signal ETF.
[[nodiscard]] std::vector<config::HistoricalDataSettings> historical_data_requests(
    std::span<const EtfDefinition> etfs
);

// Adds optional VIX plus long-leveraged instruments while deduplicating symbols.
[[nodiscard]] std::vector<config::HistoricalDataSettings> monitoring_data_requests(
    std::span<const EtfDefinition> etfs
);

// Symbols that must share a completed bar before publishing a new scan.
[[nodiscard]] std::vector<std::string> signal_symbols(
    std::span<const EtfDefinition> etfs
);

// Account positions outside the configured signal/long-leveraged universe are
// long-term portfolio holdings, not lots managed by this day-trading tool.
[[nodiscard]] std::vector<std::string> day_trade_position_symbols(
    std::span<const EtfDefinition> etfs
);

} // namespace daytrader::universe
