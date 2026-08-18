#include "daytrader/live/PositionTracker.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

namespace daytrader::live {

std::string PositionTracker::key(const std::string& account, int contract_id)
{
    return account + ':' + std::to_string(contract_id);
}

void PositionTracker::update_position(
    std::string account,
    std::string symbol,
    int contract_id,
    double quantity,
    double average_cost
)
{
    const auto position_key = key(account, contract_id);
    const auto found = std::ranges::find_if(
        positions_,
        [&](const domain::PositionSnapshot& position) {
            return key(position.account, position.contract_id) == position_key;
        }
    );
    if (std::abs(quantity) < 1e-12) {
        if (found != positions_.end()) {
            positions_.erase(found);
        }
        return;
    }

    if (found == positions_.end()) {
        positions_.push_back(domain::PositionSnapshot{
            .account = std::move(account),
            .symbol = std::move(symbol),
            .contract_id = contract_id,
            .quantity = quantity,
            .average_cost = average_cost,
        });
        return;
    }

    const bool changed_lot = std::abs(found->quantity - quantity) > 1e-12
        || std::abs(found->average_cost - average_cost) > 1e-8;
    found->symbol = std::move(symbol);
    found->quantity = quantity;
    found->average_cost = average_cost;
    if (changed_lot) {
        found->market_price.reset();
        found->market_value.reset();
        found->daily_pnl.reset();
        found->unrealized_pnl.reset();
        found->peak_unrealized_pnl.reset();
        found->giveback_amount.reset();
        found->giveback_percent.reset();
    }
}

void PositionTracker::update_pnl(
    const std::string& account,
    int contract_id,
    std::optional<double> daily_pnl,
    std::optional<double> unrealized_pnl,
    std::optional<double> market_value
)
{
    const auto position_key = key(account, contract_id);
    const auto found = std::ranges::find_if(
        positions_,
        [&](const domain::PositionSnapshot& position) {
            return key(position.account, position.contract_id) == position_key;
        }
    );
    if (found == positions_.end()) {
        return;
    }

    found->daily_pnl = daily_pnl;
    found->unrealized_pnl = unrealized_pnl;
    found->market_value = market_value;
    if (market_value.has_value() && std::abs(found->quantity) > 1e-12) {
        found->market_price = *market_value / found->quantity;
    }
    if (!unrealized_pnl.has_value()) {
        return;
    }
    if (!found->peak_unrealized_pnl.has_value()
        || *unrealized_pnl > *found->peak_unrealized_pnl) {
        found->peak_unrealized_pnl = *unrealized_pnl;
    }
    if (*found->peak_unrealized_pnl > 0.0) {
        const double giveback = *found->peak_unrealized_pnl - *unrealized_pnl;
        found->giveback_amount = std::max(0.0, giveback);
        found->giveback_percent = *found->giveback_amount
            / *found->peak_unrealized_pnl * 100.0;
    }
}

void PositionTracker::update_market_price(
    const std::string& account,
    int contract_id,
    double market_price
)
{
    if (!std::isfinite(market_price) || market_price <= 0.0) {
        return;
    }
    const auto position_key = key(account, contract_id);
    const auto found = std::ranges::find_if(
        positions_,
        [&](const domain::PositionSnapshot& position) {
            return key(position.account, position.contract_id) == position_key;
        }
    );
    if (found == positions_.end()) {
        return;
    }

    found->market_price = market_price;
    found->market_value = market_price * found->quantity;
    const double unrealized = (market_price - found->average_cost) * found->quantity;
    found->unrealized_pnl = unrealized;
    if (!found->peak_unrealized_pnl.has_value()
        || unrealized > *found->peak_unrealized_pnl) {
        found->peak_unrealized_pnl = unrealized;
    }
    if (*found->peak_unrealized_pnl > 0.0) {
        found->giveback_amount = std::max(
            0.0,
            *found->peak_unrealized_pnl - unrealized
        );
        found->giveback_percent = *found->giveback_amount
            / *found->peak_unrealized_pnl * 100.0;
    }
}

std::vector<domain::PositionSnapshot> PositionTracker::snapshot() const
{
    auto result = positions_;
    std::ranges::stable_sort(result, {}, &domain::PositionSnapshot::symbol);
    return result;
}

} // namespace daytrader::live
