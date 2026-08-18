#pragma once

#include "daytrader/domain/LiveTradeContext.hpp"

#include <optional>
#include <string>
#include <vector>

namespace daytrader::live {

// Maintains the current IBKR positions and the process-observed maximum
// favorable excursion (MFE). It has no order-placement capability.
class PositionTracker {
public:
    void update_position(
        std::string account,
        std::string symbol,
        int contract_id,
        double quantity,
        double average_cost
    );

    void update_pnl(
        const std::string& account,
        int contract_id,
        std::optional<double> daily_pnl,
        std::optional<double> unrealized_pnl,
        std::optional<double> market_value
    );

    // Fallback for accounts where reqPnLSingle is unavailable. The mark comes
    // from a normal read-only Level-1 subscription for the held contract.
    void update_market_price(
        const std::string& account,
        int contract_id,
        double market_price
    );

    [[nodiscard]] std::vector<domain::PositionSnapshot> snapshot() const;

private:
    [[nodiscard]] static std::string key(const std::string& account, int contract_id);

    std::vector<domain::PositionSnapshot> positions_;
};

} // namespace daytrader::live
