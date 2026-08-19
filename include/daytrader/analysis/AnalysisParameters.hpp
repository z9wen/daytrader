#pragma once

#include <algorithm>
#include <cstddef>

namespace daytrader::analysis {

// Centralized strategy constants keep every analyzer on the same time horizon.
inline constexpr std::size_t ema_period = 20;
inline constexpr std::size_t atr_period = 14;
inline constexpr std::size_t fast_atr_period = 5;
// Retained for 5-minute trend warm-up sizing. RelativeStrengthAnalyzer itself
// uses timestamps, so changing the source bar size cannot shorten its horizons.
inline constexpr std::size_t relative_strength_lookback_15 = 3;
inline constexpr std::size_t relative_strength_lookback_30 = 6;
inline constexpr std::size_t relative_strength_lookback = 12;
// Entry zones use a deliberately narrow quarter-ATR band around session VWAP.
inline constexpr double entry_zone_atr_half_width = 0.25;
// Live execution confirmation and MFE giveback thresholds. Profit protection
// activates only after a non-trivial peak return to avoid reacting to pennies.
inline constexpr double profit_protection_minimum_peak_return_percent = 0.25;
inline constexpr double profit_protect_giveback_percent = 20.0;
inline constexpr double profit_trim_giveback_percent = 35.0;
inline constexpr double profit_exit_giveback_percent = 50.0;
// EMA change requires one extra observation beyond the EMA seed period.
inline constexpr std::size_t minimum_analysis_bars =
    std::max(ema_period + 1, relative_strength_lookback + 1);

} // namespace daytrader::analysis
