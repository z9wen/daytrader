#pragma once

#include "Decimal.h"

#include <ctime>

namespace daytrader::ibkr {

// API 10.49 made Decimal opaque and changed callback time parameters from
// time_t to long long. Keep the application source compatible with the current
// Latest release while still allowing IBKR's supported Stable SDK to build.
#if defined(DAYTRADER_IBKR_API_10_49_OR_NEWER)
using IbkrErrorTime = long long;
using IbkrTickTime = long long;

[[nodiscard]] inline bool is_unset_decimal(Decimal value)
{
    return value.isUnset();
}
#else
using IbkrErrorTime = std::time_t;
using IbkrTickTime = std::time_t;

[[nodiscard]] inline bool is_unset_decimal(Decimal value)
{
    return value == UNSET_DECIMAL;
}
#endif

} // namespace daytrader::ibkr
