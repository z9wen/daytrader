#pragma once

#include "daytrader/domain/OrderFlow.hpp"

#include <chrono>
#include <span>
#include <vector>

namespace daytrader::analysis {

struct TradeClassifierSettings {
    std::chrono::seconds maximum_quote_age{2};
    double price_epsilon{1e-8};
};

// Applies the quote test first (ask = aggressive buy, bid = aggressive sell),
// then the tick rule for prints inside the spread or without a fresh quote.
class TradeClassifier {
public:
    explicit TradeClassifier(TradeClassifierSettings settings = {});

    [[nodiscard]] std::vector<domain::ClassifiedTrade> classify(
        std::span<const domain::TradeTick> trades,
        std::span<const domain::BidAskTick> quotes
    ) const;

private:
    TradeClassifierSettings settings_;
};

} // namespace daytrader::analysis
