#pragma once

#include "daytrader/domain/OrderFlow.hpp"

namespace daytrader::analysis {

struct OrderFlowSignalSettings {
    double balanced_delta_ratio_percent{5.0};
    double price_response_deadband_atr{0.02};
    double price_response_full_scale_atr{0.25};
    double microprice_full_scale_basis_points{0.50};
    double compressed_atr_ratio{0.80};
    double expanding_atr_ratio{1.20};
    double extreme_atr_ratio{1.60};
    std::size_t full_depth_trade_count{100};
};

// Interprets trade DeltaRatio, Level-1 OFI/microprice, and ATR-normalized price
// response. Evidence quality remains independent from directional pressure.
class OrderFlowSignalAnalyzer {
public:
    explicit OrderFlowSignalAnalyzer(OrderFlowSignalSettings settings = {});

    [[nodiscard]] domain::OrderFlowAssessment analyze(
        const domain::OrderFlowWindow& thirty_seconds,
        const domain::OrderFlowWindow& one_minute,
        double signal_atr,
        double signal_atr_expansion_ratio
    ) const;

private:
    OrderFlowSignalSettings settings_;
};

} // namespace daytrader::analysis
