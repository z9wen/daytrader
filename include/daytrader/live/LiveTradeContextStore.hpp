#pragma once

#include "daytrader/domain/LiveTradeContext.hpp"

#include <mutex>

namespace daytrader::live {

class LiveTradeContextStore {
public:
    void update(domain::LiveTradeContext context);
    [[nodiscard]] domain::LiveTradeContext snapshot() const;

private:
    mutable std::mutex mutex_;
    domain::LiveTradeContext context_;
};

} // namespace daytrader::live
