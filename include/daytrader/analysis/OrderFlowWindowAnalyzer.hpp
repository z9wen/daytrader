#pragma once

#include "daytrader/domain/OrderFlow.hpp"

#include <cstdint>
#include <span>

namespace daytrader::analysis {

class OrderFlowWindowAnalyzer {
public:
    [[nodiscard]] domain::OrderFlowWindow analyze(
        std::span<const domain::ClassifiedTrade> trades,
        std::span<const domain::BidAskTick> quotes,
        std::int64_t start_timestamp,
        std::int64_t end_timestamp
    ) const;
};

} // namespace daytrader::analysis
