#pragma once

#include <algorithm>
#include <cstddef>

namespace daytrader::analysis {

// Centralized strategy constants keep every analyzer on the same time horizon.
inline constexpr std::size_t ema_period = 20;
inline constexpr std::size_t atr_period = 14;
inline constexpr std::size_t fast_atr_period = 5;
// Twelve 5-minute bars represent the 60-minute relative-strength window.
inline constexpr std::size_t relative_strength_lookback = 12;
// Entry zones use a deliberately narrow quarter-ATR band around session VWAP.
inline constexpr double entry_zone_atr_half_width = 0.25;
// EMA change requires one extra observation beyond the EMA seed period.
inline constexpr std::size_t minimum_analysis_bars =
    std::max(ema_period + 1, relative_strength_lookback + 1);

} // namespace daytrader::analysis
