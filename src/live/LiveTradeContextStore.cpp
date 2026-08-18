#include "daytrader/live/LiveTradeContextStore.hpp"

#include <utility>

namespace daytrader::live {

void LiveTradeContextStore::update(domain::LiveTradeContext context)
{
    const std::lock_guard lock{mutex_};
    context_ = std::move(context);
}

domain::LiveTradeContext LiveTradeContextStore::snapshot() const
{
    const std::lock_guard lock{mutex_};
    return context_;
}

} // namespace daytrader::live
