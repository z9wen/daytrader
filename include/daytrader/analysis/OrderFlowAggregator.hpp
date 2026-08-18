#pragma once

#include "daytrader/domain/OrderFlow.hpp"

#include <chrono>
#include <span>
#include <vector>

namespace daytrader::analysis {

class OrderFlowAggregator {
public:
    explicit OrderFlowAggregator(std::chrono::seconds interval);

    [[nodiscard]] std::vector<domain::OrderFlowBar> aggregate(
        std::span<const domain::ClassifiedTrade> trades,
        std::span<const domain::BidAskTick> quotes
    ) const;

private:
    std::chrono::seconds interval_;
};

} // namespace daytrader::analysis
