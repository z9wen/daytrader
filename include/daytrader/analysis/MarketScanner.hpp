#pragma once

#include "daytrader/domain/InstrumentBars.hpp"
#include "daytrader/domain/MarketScan.hpp"
#include "daytrader/universe/EtfDefinition.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace daytrader::analysis {

// Orchestrates market context, relative-strength rankings, entry zones, and candidates.
class MarketScanner {
public:
    explicit MarketScanner(
        std::string time_zone,
        std::chrono::seconds bar_interval = std::chrono::minutes{5}
    );

    [[nodiscard]] domain::MarketScan scan(
        const std::vector<domain::InstrumentBars>& instruments,
        const std::vector<universe::EtfDefinition>& etfs
    ) const;

private:
    std::string time_zone_;
    std::chrono::seconds bar_interval_;
};

} // namespace daytrader::analysis
