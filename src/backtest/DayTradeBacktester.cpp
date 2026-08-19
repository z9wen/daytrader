#include "daytrader/backtest/DayTradeBacktester.hpp"

#include "daytrader/analysis/AnalysisParameters.hpp"
#include "daytrader/analysis/LeveragedExecutionAnalyzer.hpp"
#include "daytrader/analysis/LongOpportunityAnalyzer.hpp"
#include "daytrader/domain/TradeDecision.hpp"
#include "daytrader/market_data/BarTimeframeTransformer.hpp"
#include "daytrader/time/TimeZoneFormatter.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace daytrader::backtest {
namespace {

using BarIndex = std::unordered_map<std::int64_t, const domain::MarketBar*>;

struct PendingEntry {
    std::string session_date;
    domain::MarketRegime market_regime{domain::MarketRegime::neutral};
    double signal_atr{};
    double trade_atr{};
    double signal_atr_expansion_ratio{};
    double trade_atr_expansion_ratio{};
    double suggested_entry_lower{};
    double suggested_entry_upper{};
    double entry_vwap{};
};

struct Position {
    std::string session_date;
    domain::MarketRegime market_regime_at_entry{domain::MarketRegime::neutral};
    std::size_t trade_number_in_session{};
    std::int64_t entry_timestamp{};
    double signal_entry_price{};
    double entry_price{};
    double signal_atr{};
    double trade_atr{};
    double signal_atr_expansion_ratio{};
    double trade_atr_expansion_ratio{};
    double suggested_entry_lower{};
    double suggested_entry_upper{};
    double entry_vwap{};
    double stop_price{};
    double peak_price{};
    double trough_price{};
    bool trailing_active{};
    int non_strong_bars{};
};

struct PendingExit {
    ExitReason reason{ExitReason::momentum_faded};
};

// The general dashboard scanner intentionally accepts complete historical
// vectors. Replaying that scanner once per minute would repeatedly traverse
// the entire year and turn a linear backtest into quadratic work. QQQ uses a
// deliberately small decision surface (QQQ five-minute direction plus the
// TQQQ one-minute VWAP/ATR entry zone), so this state machine maintains those
// exact inputs incrementally without changing the trading rules.
class RunningEma {
public:
    explicit RunningEma(std::size_t period)
        : alpha_{2.0 / (static_cast<double>(period) + 1.0)}
    {
    }

    void update(double value)
    {
        if (!initialized_) {
            current_ = value;
            previous_ = value;
            initialized_ = true;
        } else {
            previous_ = current_;
            current_ = alpha_ * value + (1.0 - alpha_) * current_;
        }
        ++samples_;
    }

    [[nodiscard]] bool ready() const { return samples_ >= 2; }
    [[nodiscard]] double current() const { return current_; }
    [[nodiscard]] double previous() const { return previous_; }

private:
    double alpha_{};
    double current_{};
    double previous_{};
    std::size_t samples_{};
    bool initialized_{};
};

class RunningAtr {
public:
    explicit RunningAtr(std::size_t period)
        : period_{period}
    {
    }

    void update(const domain::MarketBar& bar)
    {
        const double true_range = previous_close_.has_value()
            ? std::max({
                bar.high - bar.low,
                std::abs(bar.high - *previous_close_),
                std::abs(bar.low - *previous_close_),
            })
            : bar.high - bar.low;
        previous_close_ = bar.close;
        ++samples_;
        if (samples_ <= period_) {
            seed_sum_ += true_range;
            if (samples_ == period_) {
                value_ = seed_sum_ / static_cast<double>(period_);
            }
            return;
        }
        value_ = (value_ * static_cast<double>(period_ - 1) + true_range)
            / static_cast<double>(period_);
    }

    [[nodiscard]] bool ready() const { return samples_ >= period_; }
    [[nodiscard]] double value() const { return value_; }

private:
    std::size_t period_{};
    std::size_t samples_{};
    double seed_sum_{};
    double value_{};
    std::optional<double> previous_close_;
};

class RunningSessionVwap {
public:
    void update(
        const domain::MarketBar& bar,
        const time::TimeZoneFormatter& formatter
    )
    {
        const std::string date = formatter.format_date(bar.epoch_seconds);
        const int minute = formatter.minutes_since_midnight(bar.epoch_seconds);
        const int segment = minute < 9 * 60 + 30 ? 0 : (minute < 16 * 60 ? 1 : 2);
        if (date != date_ || segment != segment_) {
            date_ = date;
            segment_ = segment;
            price_volume_sum_ = 0.0;
            volume_sum_ = 0.0;
        }
        if (!bar.volume.has_value() || *bar.volume <= 0.0) {
            return;
        }
        const double price = bar.weighted_average_price.value_or(
            (bar.high + bar.low + bar.close) / 3.0
        );
        price_volume_sum_ += price * *bar.volume;
        volume_sum_ += *bar.volume;
    }

    [[nodiscard]] std::optional<double> value() const
    {
        return volume_sum_ > 0.0
            ? std::optional<double>{price_volume_sum_ / volume_sum_}
            : std::nullopt;
    }

private:
    std::string date_;
    int segment_{-1};
    double price_volume_sum_{};
    double volume_sum_{};
};

class RunningRelativeVolume {
public:
    void update(
        const domain::MarketBar& bar,
        const time::TimeZoneFormatter& formatter
    )
    {
        const auto date = formatter.format_date(bar.epoch_seconds);
        if (date != current_date_) {
            current_date_ = date;
            current_cumulative_ = 0.0;
        }
        if (!bar.volume.has_value() || *bar.volume <= 0.0) {
            latest_ = {};
            return;
        }
        current_cumulative_ += *bar.volume;
        const int slot = formatter.minutes_since_midnight(bar.epoch_seconds);
        sessions_[date][slot] = SlotVolume{
            .bar = *bar.volume,
            .cumulative = current_cumulative_,
            .timestamp = bar.epoch_seconds,
        };
        latest_ = analyze(date, slot, bar.epoch_seconds);
    }

    [[nodiscard]] const domain::RelativeVolumeSnapshot& latest() const
    {
        return latest_;
    }

private:
    struct SlotVolume {
        double bar{};
        double cumulative{};
        std::int64_t timestamp{};
    };

    [[nodiscard]] static double median(std::vector<double> values)
    {
        std::ranges::sort(values);
        const auto middle = values.size() / 2;
        return values.size() % 2 != 0
            ? values[middle]
            : (values[middle - 1] + values[middle]) / 2.0;
    }

    [[nodiscard]] domain::RelativeVolumeSnapshot analyze(
        const std::string& current_date,
        int slot,
        std::int64_t timestamp
    ) const
    {
        constexpr std::size_t lookback_sessions = 20;
        constexpr std::size_t minimum_sessions = 3;
        constexpr std::int64_t maximum_age = 45 * 86'400;
        std::vector<double> prior_bars;
        std::vector<double> prior_cumulative;
        for (auto session = sessions_.rbegin(); session != sessions_.rend(); ++session) {
            if (session->first >= current_date) {
                continue;
            }
            const auto found = session->second.find(slot);
            if (found == session->second.end()
                || found->second.timestamp < timestamp - maximum_age) {
                continue;
            }
            prior_bars.push_back(found->second.bar);
            prior_cumulative.push_back(found->second.cumulative);
            if (prior_bars.size() == lookback_sessions) {
                break;
            }
        }

        domain::RelativeVolumeSnapshot result{
            .baseline_sessions = prior_bars.size(),
        };
        if (prior_bars.size() < minimum_sessions) {
            return result;
        }
        const auto& current = sessions_.at(current_date).at(slot);
        const double normal_bar = median(std::move(prior_bars));
        const double normal_cumulative = median(std::move(prior_cumulative));
        if (normal_bar > 0.0) {
            result.bar_ratio = current.bar / normal_bar;
        }
        if (normal_cumulative > 0.0) {
            result.cumulative_ratio = current.cumulative / normal_cumulative;
        }
        const auto reference = result.bar_ratio.has_value()
            ? result.bar_ratio
            : result.cumulative_ratio;
        if (!reference.has_value()) {
            return result;
        }
        if (*reference < 0.80) {
            result.state = domain::RelativeVolumeState::light;
        } else if (*reference >= 1.20) {
            result.state = domain::RelativeVolumeState::expanding;
        } else {
            result.state = domain::RelativeVolumeState::normal;
        }
        return result;
    }

    std::map<std::string, std::map<int, SlotVolume>> sessions_;
    std::string current_date_;
    double current_cumulative_{};
    domain::RelativeVolumeSnapshot latest_;
};

class RunningInstrumentMetrics {
public:
    void update(
        const domain::MarketBar& bar,
        const time::TimeZoneFormatter& formatter
    )
    {
        const auto previous = samples_ > 0
            ? std::optional<domain::MarketBar>{latest_}
            : std::nullopt;
        const auto previous_vwap = vwap_.value();
        latest_ = bar;
        ema_.update(bar.close);
        atr14_.update(bar);
        atr5_.update(bar);
        vwap_.update(bar, formatter);
        relative_volume_.update(bar, formatter);
        const auto current_vwap = vwap_.value();
        if (!current_vwap.has_value()) {
            vwap_structure_ = domain::VwapStructureState::unavailable;
        } else if (!previous.has_value() || !previous_vwap.has_value()
                   || formatter.format_date(previous->epoch_seconds)
                        != formatter.format_date(bar.epoch_seconds)
                   || is_regular(previous->epoch_seconds, formatter)
                        != is_regular(bar.epoch_seconds, formatter)) {
            vwap_structure_ = bar.close >= *current_vwap
                ? domain::VwapStructureState::above_flat
                : domain::VwapStructureState::below;
        } else if (previous->close <= *previous_vwap && bar.close > *current_vwap) {
            vwap_structure_ = domain::VwapStructureState::reclaimed;
        } else if (previous->close >= *previous_vwap && bar.close < *current_vwap) {
            vwap_structure_ = domain::VwapStructureState::lost;
        } else if (bar.close < *current_vwap) {
            vwap_structure_ = domain::VwapStructureState::below;
        } else {
            const double tolerance = std::max(1e-8, std::abs(*current_vwap) * 1e-6);
            vwap_structure_ = *current_vwap > *previous_vwap + tolerance
                ? domain::VwapStructureState::above_rising
                : domain::VwapStructureState::above_flat;
        }
        ++samples_;
    }

    [[nodiscard]] bool analysis_ready() const
    {
        return samples_ >= analysis::minimum_analysis_bars && ema_.ready()
            && atr14_.ready() && atr5_.ready() && vwap_.value().has_value();
    }

    [[nodiscard]] std::int64_t latest_timestamp() const
    {
        return latest_.epoch_seconds;
    }

    [[nodiscard]] const domain::MarketBar& latest_bar() const
    {
        return latest_;
    }

    [[nodiscard]] domain::EtfSnapshot snapshot(std::string symbol) const
    {
        domain::EtfSnapshot result{
            .symbol = std::move(symbol),
            .close = latest_.close,
            .session_vwap = vwap_.value(),
            .ema20 = ema_.current(),
            .ema20_change_percent = ema_.previous() == 0.0
                ? 0.0
                : (ema_.current() / ema_.previous() - 1.0) * 100.0,
            .atr14 = atr14_.value(),
            .atr_expansion_ratio = atr14_.value() > 0.0
                ? atr5_.value() / atr14_.value()
                : 1.0,
            .vwap_structure = vwap_structure_,
            .relative_volume = relative_volume_.latest(),
        };
        if (result.session_vwap.has_value()
            && result.close > *result.session_vwap
            && result.ema20_change_percent > 0.0) {
            result.trend_signal = domain::MarketTrendSignal::strong;
        } else if (result.session_vwap.has_value()
                   && result.close < *result.session_vwap
                   && result.ema20_change_percent < 0.0) {
            result.trend_signal = domain::MarketTrendSignal::weak;
        }
        return result;
    }

    [[nodiscard]] std::optional<domain::EntryZone> entry_zone(
        std::string symbol
    ) const
    {
        const auto session_vwap = vwap_.value();
        if (!analysis_ready() || !session_vwap.has_value() || atr14_.value() <= 0.0) {
            return std::nullopt;
        }
        const double half_width = atr14_.value()
            * analysis::entry_zone_atr_half_width;
        const double lower = *session_vwap - half_width;
        const double upper = *session_vwap + half_width;
        const auto state = latest_.close > upper
            ? domain::EntryZoneState::extended
            : (latest_.close < lower
                ? domain::EntryZoneState::below_zone
                : domain::EntryZoneState::in_zone);
        return domain::EntryZone{
            .symbol = std::move(symbol),
            .lower_price = std::max(0.0, lower),
            .upper_price = upper,
            .current_price = latest_.close,
            .session_vwap = *session_vwap,
            .atr14 = atr14_.value(),
            .atr5 = atr5_.value(),
            .atr_percent = latest_.close > 0.0
                ? atr14_.value() / latest_.close * 100.0
                : 0.0,
            .atr_expansion_ratio = atr5_.value() / atr14_.value(),
            .state = state,
        };
    }

private:
    [[nodiscard]] static bool is_regular(
        std::int64_t timestamp,
        const time::TimeZoneFormatter& formatter
    )
    {
        const int minute = formatter.minutes_since_midnight(timestamp);
        return minute >= 9 * 60 + 30 && minute < 16 * 60;
    }

    domain::MarketBar latest_;
    RunningEma ema_{analysis::ema_period};
    RunningAtr atr14_{analysis::atr_period};
    RunningAtr atr5_{analysis::fast_atr_period};
    RunningSessionVwap vwap_;
    RunningRelativeVolume relative_volume_;
    domain::VwapStructureState vwap_structure_{
        domain::VwapStructureState::unavailable
    };
    std::size_t samples_{};
};

class RunningRelativeStrength {
public:
    void update(
        std::int64_t timestamp,
        double signal_close,
        double benchmark_close
    )
    {
        if (timestamp == latest_timestamp_) {
            return;
        }
        if (signal_close <= 0.0 || benchmark_close <= 0.0) {
            throw std::runtime_error(
                "relative strength requires positive close prices"
            );
        }
        const double ratio = signal_close / benchmark_close;
        ratio_ema_.update(ratio);
        samples_.push_back(Sample{.timestamp = timestamp, .ratio = ratio});
        if (samples_.size() > 32) {
            samples_.erase(samples_.begin());
        }
        latest_timestamp_ = timestamp;
    }

    [[nodiscard]] bool ready() const
    {
        return ratio_ema_.ready() && samples_.size() >= 13;
    }

    [[nodiscard]] double ratio() const { return samples_.back().ratio; }
    [[nodiscard]] double ema() const { return ratio_ema_.current(); }

    [[nodiscard]] domain::RelativeStrengthHorizons horizons() const
    {
        return domain::RelativeStrengthHorizons{
            .fifteen_minute_percent = change(std::chrono::minutes{15}),
            .thirty_minute_percent = change(std::chrono::minutes{30}),
            .sixty_minute_percent = change(std::chrono::minutes{60}),
        };
    }

private:
    struct Sample {
        std::int64_t timestamp{};
        double ratio{};
    };

    [[nodiscard]] std::optional<double> change(std::chrono::minutes horizon) const
    {
        if (samples_.size() < 2) {
            return std::nullopt;
        }
        const auto target = samples_.back().timestamp - horizon.count() * 60;
        const auto after = std::ranges::upper_bound(
            samples_, target, {}, &Sample::timestamp
        );
        if (after == samples_.begin()) {
            return std::nullopt;
        }
        const double prior = std::prev(after)->ratio;
        return prior > 0.0
            ? std::optional<double>{(samples_.back().ratio / prior - 1.0) * 100.0}
            : std::nullopt;
    }

    RunningEma ratio_ema_{analysis::ema_period};
    std::vector<Sample> samples_;
    std::int64_t latest_timestamp_{-1};
};

// Incrementally reproduces the inputs used by the dashboard for either the
// QQQ -> TQQQ market pair or an industry -> leveraged-industry pair. This
// keeps a YTD one-minute replay linear while preserving SOXX relative strength,
// relative volume and its independent VWAP structure.
class PairReplayEngine {
public:
    PairReplayEngine(
        std::string time_zone,
        std::string signal_symbol,
        std::string trade_symbol
    )
        : formatter_{std::move(time_zone)}
        , signal_symbol_{std::move(signal_symbol)}
        , trade_symbol_{std::move(trade_symbol)}
    {
    }

    void update_execution(const std::string& symbol, const domain::MarketBar& bar)
    {
        if (symbol == signal_symbol_) {
            signal_execution_.update(bar, formatter_);
        } else if (symbol == trade_symbol_) {
            trade_execution_.update(bar, formatter_);
        }
    }

    void update_trend(const std::string& symbol, const domain::MarketBar& bar)
    {
        if (symbol == "SPY") {
            spy_trend_.update(bar, formatter_);
        }
        if (symbol == "QQQ") {
            qqq_trend_.update(bar, formatter_);
        }
        if (symbol == signal_symbol_) {
            signal_trend_.update(bar, formatter_);
            trend_timestamp_ = bar.epoch_seconds;
            if (spy_trend_.latest_timestamp() == trend_timestamp_
                && qqq_trend_.latest_timestamp() == trend_timestamp_) {
                signal_vs_spy_.update(
                    trend_timestamp_,
                    signal_trend_.latest_bar().close,
                    spy_trend_.latest_bar().close
                );
                signal_vs_qqq_.update(
                    trend_timestamp_,
                    signal_trend_.latest_bar().close,
                    qqq_trend_.latest_bar().close
                );
                relative_timestamp_ = trend_timestamp_;
            }
        }
    }

    [[nodiscard]] bool ready() const
    {
        return spy_trend_.analysis_ready() && qqq_trend_.analysis_ready()
            && signal_trend_.analysis_ready()
            && signal_execution_.analysis_ready()
            && trade_execution_.analysis_ready();
    }

    [[nodiscard]] domain::MarketScan scan()
    {
        auto spy = spy_trend_.snapshot("SPY");
        auto qqq = qqq_trend_.snapshot("QQQ");
        auto signal_trend = signal_trend_.snapshot(signal_symbol_);
        auto signal_execution = signal_execution_.snapshot(signal_symbol_);
        if (relative_timestamp_ != trend_timestamp_) {
            signal_vs_spy_.update(
                trend_timestamp_,
                signal_trend_.latest_bar().close,
                spy_trend_.latest_bar().close
            );
            signal_vs_qqq_.update(
                trend_timestamp_,
                signal_trend_.latest_bar().close,
                qqq_trend_.latest_bar().close
            );
            relative_timestamp_ = trend_timestamp_;
        }
        auto market_regime = domain::MarketRegime::neutral;
        if (spy.trend_signal == domain::MarketTrendSignal::strong
            && qqq.trend_signal == domain::MarketTrendSignal::strong) {
            market_regime = domain::MarketRegime::bullish;
        } else if (spy.trend_signal == domain::MarketTrendSignal::weak
                   && qqq.trend_signal == domain::MarketTrendSignal::weak) {
            market_regime = domain::MarketRegime::bearish;
        }

        domain::RankedEtf rank{
            .symbol = signal_symbol_,
            .name = signal_symbol_,
            .group = signal_symbol_ == "QQQ" ? "MARKET" : "INDUSTRY",
            .benchmark_symbol = "SPY",
            .leveraged_long_symbol = trade_symbol_,
            .close = signal_execution.close,
            .session_vwap = signal_execution.session_vwap,
            .ema20 = signal_trend.ema20,
            .ema20_change_percent = signal_trend.ema20_change_percent,
            .vwap_structure = signal_execution.vwap_structure,
            .relative_volume = signal_execution.relative_volume,
            .entry_zone = signal_execution_.entry_zone(signal_symbol_),
            .leveraged_entry_zone = trade_execution_.entry_zone(trade_symbol_),
        };
        if (signal_vs_spy_.ready() && signal_vs_qqq_.ready()) {
            rank.relative_ratio = signal_vs_spy_.ratio();
            rank.relative_ratio_ema20 = signal_vs_spy_.ema();
            rank.relative_strength_vs_spy = signal_vs_spy_.horizons();
            rank.relative_strength_vs_qqq = signal_vs_qqq_.horizons();
            rank.relative_change_60_min_percent =
                rank.relative_strength_vs_spy.sixty_minute_percent.value_or(0.0);
            if (rank.relative_ratio > rank.relative_ratio_ema20
                && rank.relative_change_60_min_percent > 0.0) {
                rank.signal = domain::RelativeStrengthSignal::strong;
            } else if (rank.relative_ratio < rank.relative_ratio_ema20
                       && rank.relative_change_60_min_percent < 0.0) {
                rank.signal = domain::RelativeStrengthSignal::weak;
            }
        }
        if (signal_symbol_ != "QQQ") {
            rank.long_opportunity = analysis::LongOpportunityAnalyzer{}.analyze(
                rank,
                market_regime
            );
        }
        domain::MarketScan result{
            .epoch_seconds = trend_timestamp_,
            .spy = std::move(spy),
            .qqq = std::move(qqq),
            .market_regime = market_regime,
        };
        result.rankings.push_back(std::move(rank));
        return result;
    }

private:
    time::TimeZoneFormatter formatter_;
    std::string signal_symbol_;
    std::string trade_symbol_;
    RunningInstrumentMetrics spy_trend_;
    RunningInstrumentMetrics qqq_trend_;
    RunningInstrumentMetrics signal_trend_;
    RunningInstrumentMetrics signal_execution_;
    RunningInstrumentMetrics trade_execution_;
    RunningRelativeStrength signal_vs_spy_;
    RunningRelativeStrength signal_vs_qqq_;
    std::int64_t trend_timestamp_{};
    std::int64_t relative_timestamp_{-1};
};

enum class CyclePhase {
    building,
    strong,
    neutral,
    fading,
    weak,
};

[[nodiscard]] CyclePhase market_cycle_phase(const domain::EtfSnapshot& signal)
{
    if (signal.trend_signal == domain::MarketTrendSignal::strong) {
        return CyclePhase::strong;
    }
    if (signal.trend_signal == domain::MarketTrendSignal::weak) {
        return CyclePhase::weak;
    }
    if (!signal.session_vwap.has_value()) {
        return CyclePhase::neutral;
    }
    // Momentum improving while price is still below VWAP is the accumulation
    // stage the user calls BUILDING. The mirror image is a fading trend.
    if (signal.close <= *signal.session_vwap
        && signal.ema20_change_percent > 0.0) {
        return CyclePhase::building;
    }
    if (signal.close >= *signal.session_vwap
        && signal.ema20_change_percent < 0.0) {
        return CyclePhase::fading;
    }
    return CyclePhase::neutral;
}

[[nodiscard]] CyclePhase cycle_phase(
    const DayTradeBacktestSettings& settings,
    const domain::MarketScan& scan,
    const domain::RankedEtf& rank
)
{
    if (settings.signal_symbol == "QQQ") {
        return market_cycle_phase(scan.qqq);
    }
    switch (rank.long_opportunity.phase) {
    case domain::BullishPhase::building:
        return CyclePhase::building;
    case domain::BullishPhase::strong:
        return CyclePhase::strong;
    case domain::BullishPhase::neutral:
        return CyclePhase::neutral;
    case domain::BullishPhase::fading:
        return CyclePhase::fading;
    case domain::BullishPhase::weak:
        return CyclePhase::weak;
    }
    return CyclePhase::neutral;
}

[[nodiscard]] const domain::InstrumentBars& require_instrument(
    const std::vector<domain::InstrumentBars>& instruments,
    const std::string& symbol
)
{
    const auto found = std::ranges::find(instruments, symbol, &domain::InstrumentBars::symbol);
    if (found == instruments.end()) {
        throw std::invalid_argument("backtest data is missing " + symbol);
    }
    return *found;
}

[[nodiscard]] BarIndex index_bars(const domain::InstrumentBars& instrument)
{
    BarIndex index;
    index.reserve(instrument.bars.size());
    for (const auto& bar : instrument.bars) {
        index.insert_or_assign(bar.epoch_seconds, &bar);
    }
    return index;
}

[[nodiscard]] std::vector<std::int64_t> common_timestamps(
    const std::vector<BarIndex>& indexes
)
{
    if (indexes.empty()) {
        return {};
    }

    std::vector<std::int64_t> timestamps;
    timestamps.reserve(indexes.front().size());
    for (const auto& [timestamp, unused] : indexes.front()) {
        static_cast<void>(unused);
        const bool present_everywhere = std::ranges::all_of(
            indexes | std::views::drop(1),
            [timestamp](const BarIndex& index) { return index.contains(timestamp); }
        );
        if (present_everywhere) {
            timestamps.push_back(timestamp);
        }
    }
    std::ranges::sort(timestamps);
    return timestamps;
}

[[nodiscard]] domain::PositionSnapshot simulated_position_snapshot(
    const Position& position,
    double current_price
)
{
    const double peak_profit = std::max(
        0.0,
        position.peak_price - position.entry_price
    );
    const double current_profit = current_price - position.entry_price;
    const double giveback = std::max(0.0, peak_profit - current_profit);
    return domain::PositionSnapshot{
        .symbol = {},
        .quantity = 1.0,
        .average_cost = position.entry_price,
        .market_price = current_price,
        .unrealized_pnl = current_profit,
        .peak_unrealized_pnl = peak_profit,
        .giveback_amount = giveback,
        .giveback_percent = peak_profit > 0.0
            ? std::optional<double>{giveback / peak_profit * 100.0}
            : std::nullopt,
    };
}

[[nodiscard]] domain::LeveragedExecutionDecision historical_execution(
    const DayTradeBacktestSettings& settings,
    const domain::MarketScan& scan,
    const domain::RankedEtf& rank,
    const domain::PositionSnapshot* position
)
{
    const analysis::LeveragedExecutionAnalyzer analyzer;
    if (settings.signal_symbol == "QQQ") {
        return analyzer.analyze_market(
            scan.qqq,
            rank.leveraged_entry_zone,
            std::nullopt,
            position,
            false
        );
    }
    return analyzer.analyze(
        rank.long_opportunity,
        rank.leveraged_entry_zone,
        std::nullopt,
        position,
        false
    );
}

void append_trade(
    BacktestReport& report,
    const DayTradeBacktestSettings& settings,
    const Position& position,
    std::int64_t exit_timestamp,
    double exit_price,
    ExitReason reason
)
{
    const double gross_return = ((exit_price / position.entry_price) - 1.0) * 100.0;
    const double round_trip_cost = settings.per_side_cost_basis_points * 2.0 / 100.0;
    const double net_return = gross_return - round_trip_cost;
    report.trade_log.push_back(TradeRecord{
        .session_date = position.session_date,
        .market_regime_at_entry = position.market_regime_at_entry,
        .trade_number_in_session = position.trade_number_in_session,
        .entry_timestamp = position.entry_timestamp,
        .exit_timestamp = exit_timestamp,
        .signal_entry_price = position.signal_entry_price,
        .entry_price = position.entry_price,
        .exit_price = exit_price,
        .suggested_entry_lower = position.suggested_entry_lower,
        .suggested_entry_upper = position.suggested_entry_upper,
        .entry_vwap = position.entry_vwap,
        .signal_atr_at_entry = position.signal_atr,
        .trade_atr_at_entry = position.trade_atr,
        .signal_atr_percent_at_entry = position.signal_entry_price > 0.0
            ? position.signal_atr / position.signal_entry_price * 100.0
            : 0.0,
        .trade_atr_percent_at_entry = position.entry_price > 0.0
            ? position.trade_atr / position.entry_price * 100.0
            : 0.0,
        .signal_atr_expansion_ratio = position.signal_atr_expansion_ratio,
        .trade_atr_expansion_ratio = position.trade_atr_expansion_ratio,
        .gross_return_percent = gross_return,
        .net_return_percent = net_return,
        .maximum_favorable_excursion_percent =
            ((position.peak_price / position.entry_price) - 1.0) * 100.0,
        .maximum_adverse_excursion_percent =
            ((position.trough_price / position.entry_price) - 1.0) * 100.0,
        .exit_reason = reason,
    });
}

void evaluate_trade_diagnostics(
    BacktestReport& report,
    std::span<const domain::MarketBar> trade_bars
)
{
    constexpr std::int64_t entry_horizon_seconds = 30 * 60;
    constexpr std::int64_t exit_horizon_seconds = 15 * 60;
    constexpr double entry_target_atr = 0.75;
    constexpr double false_breakout_atr = 0.40;
    constexpr double exit_direction_atr = 0.50;

    if (trade_bars.empty()) {
        return;
    }
    for (auto& trade : report.trade_log) {
        if (trade.trade_atr_at_entry <= 0.0 || trade.entry_price <= 0.0
            || trade.exit_price <= 0.0) {
            continue;
        }

        const auto entry_deadline = trade.entry_timestamp + entry_horizon_seconds;
        const double target_price = trade.entry_price
            + entry_target_atr * trade.trade_atr_at_entry;
        const double false_breakout_price = trade.entry_price
            - false_breakout_atr * trade.trade_atr_at_entry;
        auto iterator = std::ranges::lower_bound(
            trade_bars,
            trade.entry_timestamp,
            {},
            &domain::MarketBar::epoch_seconds
        );
        for (; iterator != trade_bars.end()
               && iterator->epoch_seconds <= entry_deadline; ++iterator) {
            const bool target_hit = iterator->high >= target_price;
            const bool false_breakout_hit = iterator->low <= false_breakout_price;
            if (target_hit && false_breakout_hit) {
                trade.entry_follow_through = EntryFollowThroughOutcome::ambiguous;
            } else if (target_hit) {
                trade.entry_follow_through =
                    EntryFollowThroughOutcome::follow_through;
            } else if (false_breakout_hit) {
                trade.entry_follow_through =
                    EntryFollowThroughOutcome::false_breakout;
            }
            if (trade.entry_follow_through
                != EntryFollowThroughOutcome::insufficient_data) {
                break;
            }
        }
        if (trade.entry_follow_through
                == EntryFollowThroughOutcome::insufficient_data
            && trade_bars.back().epoch_seconds >= entry_deadline) {
            trade.entry_follow_through =
                EntryFollowThroughOutcome::no_follow_through;
        }

        const auto exit_deadline = trade.exit_timestamp + exit_horizon_seconds;
        const double continuation_price = trade.exit_price
            + exit_direction_atr * trade.trade_atr_at_entry;
        const double protection_price = trade.exit_price
            - exit_direction_atr * trade.trade_atr_at_entry;
        iterator = std::ranges::upper_bound(
            trade_bars,
            trade.exit_timestamp,
            {},
            &domain::MarketBar::epoch_seconds
        );
        for (; iterator != trade_bars.end()
               && iterator->epoch_seconds <= exit_deadline; ++iterator) {
            const bool continued_up = iterator->high >= continuation_price;
            const bool moved_down = iterator->low <= protection_price;
            if (continued_up && moved_down) {
                trade.exit_timing = ExitTimingOutcome::ambiguous;
            } else if (moved_down) {
                trade.exit_timing = ExitTimingOutcome::protected_capital;
            } else if (continued_up) {
                trade.exit_timing = ExitTimingOutcome::premature;
            }
            if (trade.exit_timing != ExitTimingOutcome::insufficient_data) {
                break;
            }
        }
        if (trade.exit_timing == ExitTimingOutcome::insufficient_data
            && trade_bars.back().epoch_seconds >= exit_deadline) {
            trade.exit_timing = ExitTimingOutcome::neutral;
        }

        if (trade.maximum_favorable_excursion_percent > 1e-9) {
            trade.profit_capture_percent = std::clamp(
                trade.gross_return_percent
                    / trade.maximum_favorable_excursion_percent * 100.0,
                0.0,
                100.0
            );
        }
    }
}

void summarize(BacktestReport& report)
{
    report.trades = report.trade_log.size();
    if (report.trade_log.empty()) {
        return;
    }

    std::unordered_map<std::string, std::size_t> trades_per_session;
    double return_sum{};
    double win_sum{};
    double loss_sum{};
    double holding_minutes_sum{};
    double mfe_sum{};
    double profit_capture_sum{};
    std::size_t profit_capture_samples{};
    double equity = 1.0;
    double peak_equity = 1.0;
    double maximum_drawdown{};

    for (const auto& trade : report.trade_log) {
        ++trades_per_session[trade.session_date];
        return_sum += trade.net_return_percent;
        holding_minutes_sum += static_cast<double>(
            trade.exit_timestamp - trade.entry_timestamp
        ) / 60.0;
        mfe_sum += trade.maximum_favorable_excursion_percent;
        if (trade.profit_capture_percent.has_value()) {
            profit_capture_sum += *trade.profit_capture_percent;
            ++profit_capture_samples;
        }
        switch (trade.entry_follow_through) {
        case EntryFollowThroughOutcome::follow_through:
            ++report.entry_outcome_samples;
            ++report.entry_follow_throughs;
            break;
        case EntryFollowThroughOutcome::false_breakout:
            ++report.entry_outcome_samples;
            ++report.false_breakouts;
            break;
        case EntryFollowThroughOutcome::no_follow_through:
            ++report.entry_outcome_samples;
            ++report.no_follow_throughs;
            break;
        case EntryFollowThroughOutcome::ambiguous:
            ++report.ambiguous_entries;
            break;
        case EntryFollowThroughOutcome::insufficient_data:
            break;
        }
        switch (trade.exit_timing) {
        case ExitTimingOutcome::protected_capital:
            ++report.directional_exit_samples;
            ++report.protected_exits;
            break;
        case ExitTimingOutcome::premature:
            ++report.directional_exit_samples;
            ++report.premature_exits;
            break;
        case ExitTimingOutcome::neutral:
            ++report.neutral_exits;
            break;
        case ExitTimingOutcome::ambiguous:
            ++report.ambiguous_exits;
            break;
        case ExitTimingOutcome::insufficient_data:
            break;
        }
        if (trade.net_return_percent > 0.0) {
            ++report.wins;
            win_sum += trade.net_return_percent;
        } else {
            ++report.losses;
            loss_sum += trade.net_return_percent;
        }

        equity *= 1.0 + trade.net_return_percent / 100.0;
        peak_equity = std::max(peak_equity, equity);
        if (peak_equity > 0.0) {
            maximum_drawdown = std::max(
                maximum_drawdown,
                (peak_equity - equity) / peak_equity * 100.0
            );
        }
    }

    report.traded_sessions = trades_per_session.size();
    for (const auto& [session, trade_count] : trades_per_session) {
        static_cast<void>(session);
        report.maximum_trades_in_session = std::max(
            report.maximum_trades_in_session,
            trade_count
        );
    }

    const auto trade_count = static_cast<double>(report.trades);
    report.win_rate_percent = static_cast<double>(report.wins) / trade_count * 100.0;
    report.average_net_return_percent = return_sum / trade_count;
    report.average_win_percent = report.wins == 0
        ? 0.0
        : win_sum / static_cast<double>(report.wins);
    report.average_loss_percent = report.losses == 0
        ? 0.0
        : loss_sum / static_cast<double>(report.losses);
    report.compounded_return_percent = (equity - 1.0) * 100.0;
    report.profit_factor = loss_sum == 0.0
        ? std::numeric_limits<double>::infinity()
        : win_sum / std::abs(loss_sum);
    report.maximum_drawdown_percent = maximum_drawdown;
    report.average_holding_minutes = holding_minutes_sum / trade_count;
    report.average_mfe_percent = mfe_sum / trade_count;
    if (report.entry_outcome_samples > 0) {
        const double entry_samples = static_cast<double>(report.entry_outcome_samples);
        report.entry_follow_through_rate_percent =
            static_cast<double>(report.entry_follow_throughs) / entry_samples * 100.0;
        report.false_breakout_rate_percent =
            static_cast<double>(report.false_breakouts) / entry_samples * 100.0;
    }
    if (report.directional_exit_samples > 0) {
        const double exit_samples = static_cast<double>(report.directional_exit_samples);
        report.exit_timing_accuracy_percent =
            static_cast<double>(report.protected_exits) / exit_samples * 100.0;
        report.premature_exit_rate_percent =
            static_cast<double>(report.premature_exits) / exit_samples * 100.0;
    }
    report.average_profit_capture_percent = profit_capture_samples == 0
        ? 0.0
        : profit_capture_sum / static_cast<double>(profit_capture_samples);
}

} // namespace

DayTradeBacktester::DayTradeBacktester(DayTradeBacktestSettings settings)
    : settings_{std::move(settings)}
{
    if (settings_.strategy_name.empty()) {
        throw std::invalid_argument("backtest strategy name cannot be empty");
    }
    if (settings_.source_bar_interval <= std::chrono::seconds::zero()
        || settings_.trend_bar_interval < settings_.source_bar_interval
        || settings_.trend_bar_interval.count()
            % settings_.source_bar_interval.count() != 0) {
        throw std::invalid_argument("backtest bar intervals are invalid");
    }
    if (settings_.entry_start_minute.has_value()
        && settings_.entry_end_minute.has_value()
        && *settings_.entry_start_minute >= *settings_.entry_end_minute) {
        throw std::invalid_argument("backtest entry window is invalid");
    }
    if (settings_.initial_stop_atr <= 0.0 || settings_.trailing_activation_atr <= 0.0
        || settings_.trailing_distance_atr <= 0.0) {
        throw std::invalid_argument("backtest ATR multipliers must be positive");
    }
    if (settings_.per_side_cost_basis_points < 0.0) {
        throw std::invalid_argument("backtest trading cost cannot be negative");
    }
}

BacktestReport DayTradeBacktester::run(
    const std::vector<domain::InstrumentBars>& instruments
) const
{
    std::vector<std::string> symbols{"SPY", "QQQ"};
    for (const auto* symbol : {&settings_.signal_symbol, &settings_.trade_symbol}) {
        if (std::ranges::find(symbols, *symbol) == symbols.end()) {
            symbols.push_back(*symbol);
        }
    }
    std::vector<BarIndex> indexes;
    indexes.reserve(symbols.size());
    for (const auto& symbol : symbols) {
        const auto& source = require_instrument(instruments, symbol);
        indexes.push_back(index_bars(source));
    }

    const auto timestamps = common_timestamps(indexes);
    if (timestamps.empty()) {
        throw std::runtime_error("backtest instruments have no common bars");
    }

    std::vector<domain::InstrumentBars> derived_trend_instruments;
    const std::vector<domain::InstrumentBars>* trend_instruments = &instruments;
    if (settings_.source_bar_interval < settings_.trend_bar_interval) {
        derived_trend_instruments = market_data::resample_bars(
            instruments,
            settings_.source_bar_interval,
            settings_.trend_bar_interval
        );
        trend_instruments = &derived_trend_instruments;
    }
    std::vector<const domain::InstrumentBars*> trend_series;
    trend_series.reserve(symbols.size());
    for (const auto& symbol : symbols) {
        trend_series.push_back(&require_instrument(*trend_instruments, symbol));
    }
    std::vector<std::size_t> trend_offsets(symbols.size());

    const auto signal_index = static_cast<std::size_t>(
        std::ranges::find(symbols, settings_.signal_symbol) - symbols.begin()
    );
    const auto trade_index = static_cast<std::size_t>(
        std::ranges::find(symbols, settings_.trade_symbol) - symbols.begin()
    );
    PairReplayEngine pair_replay{
        settings_.time_zone,
        settings_.signal_symbol,
        settings_.trade_symbol,
    };

    BacktestReport report{
        .strategy_name = settings_.strategy_name,
        .signal_symbol = settings_.signal_symbol,
        .trade_symbol = settings_.trade_symbol,
    };
    const time::TimeZoneFormatter formatter{settings_.time_zone};
    std::set<std::string> sessions;
    std::optional<PendingEntry> pending_entry;
    std::optional<PendingExit> pending_exit;
    std::optional<Position> position;
    std::optional<domain::MarketBar> previous_trade_bar;
    std::string current_session;
    // Require a genuine non-ready state between entries so one continuous
    // signal cannot create duplicate fills, without imposing a daily count cap.
    bool entry_armed{true};
    bool cycle_building_seen{};
    std::optional<std::int64_t> last_cycle_trend_timestamp;
    std::size_t trades_this_session{};

    for (const auto timestamp : timestamps) {
        for (std::size_t index = 0; index < indexes.size(); ++index) {
            const auto& execution_bar = *indexes[index].at(timestamp);
            pair_replay.update_execution(symbols[index], execution_bar);
            auto& offset = trend_offsets[index];
            const auto& source = trend_series[index]->bars;
            const auto completed_at = settings_.trend_bar_interval.count()
                - settings_.source_bar_interval.count();
            while (offset < source.size()
                   && source[offset].epoch_seconds + completed_at <= timestamp) {
                pair_replay.update_trend(symbols[index], source[offset]);
                ++offset;
            }
        }
        const auto& trade_bar = *indexes[trade_index].at(timestamp);
        const auto& signal_bar = *indexes[signal_index].at(timestamp);
        const std::string session = formatter.format_date(timestamp);
        const int minute = formatter.minutes_since_midnight(timestamp);
        const bool in_test_window = !settings_.earliest_entry_timestamp.has_value()
            || timestamp >= *settings_.earliest_entry_timestamp;
        if (in_test_window) {
            sessions.insert(session);
        }

        if (!current_session.empty() && session != current_session) {
            if (position.has_value() && previous_trade_bar.has_value()) {
                append_trade(
                    report,
                    settings_,
                    *position,
                    previous_trade_bar->epoch_seconds,
                    previous_trade_bar->close,
                    ExitReason::session_end
                );
                position.reset();
            }
            pending_entry.reset();
            pending_exit.reset();
            entry_armed = true;
            cycle_building_seen = false;
            last_cycle_trend_timestamp.reset();
            trades_this_session = 0;
        }
        current_session = session;

        // Decisions are created from the prior completed bar, so scheduled
        // actions execute at this bar's open before this bar is analyzed.
        if (position.has_value() && pending_exit.has_value()) {
            append_trade(
                report,
                settings_,
                *position,
                timestamp,
                trade_bar.open,
                pending_exit->reason
            );
            position.reset();
            pending_exit.reset();
        }
        if (!position.has_value() && pending_entry.has_value()) {
            if (pending_entry->session_date == session) {
                position = Position{
                    .session_date = session,
                    .market_regime_at_entry = pending_entry->market_regime,
                    .trade_number_in_session = trades_this_session + 1,
                    .entry_timestamp = timestamp,
                    .signal_entry_price = signal_bar.open,
                    .entry_price = trade_bar.open,
                    .signal_atr = pending_entry->signal_atr,
                    .trade_atr = pending_entry->trade_atr,
                    .signal_atr_expansion_ratio =
                        pending_entry->signal_atr_expansion_ratio,
                    .trade_atr_expansion_ratio =
                        pending_entry->trade_atr_expansion_ratio,
                    .suggested_entry_lower = pending_entry->suggested_entry_lower,
                    .suggested_entry_upper = pending_entry->suggested_entry_upper,
                    .entry_vwap = pending_entry->entry_vwap,
                    .stop_price = trade_bar.open
                        - settings_.initial_stop_atr * pending_entry->trade_atr,
                    .peak_price = trade_bar.open,
                    .trough_price = trade_bar.open,
                };
                ++trades_this_session;
                entry_armed = false;
            }
            pending_entry.reset();
        }

        if (position.has_value()) {
            const bool stopped_at_open = trade_bar.open <= position->stop_price;
            const bool stopped_intrabar = trade_bar.low <= position->stop_price;
            if (stopped_at_open || stopped_intrabar) {
                const double exit_price = stopped_at_open
                    ? trade_bar.open
                    : position->stop_price;
                const auto reason = position->trailing_active
                    ? ExitReason::trailing_stop
                    : ExitReason::protective_stop;
                append_trade(report, settings_, *position, timestamp, exit_price, reason);
                position.reset();
            }
        }

        if (!pair_replay.ready()) {
            previous_trade_bar = trade_bar;
            continue;
        }

        const auto scan = pair_replay.scan();
        const auto rank = std::ranges::find(
            scan.rankings,
            settings_.signal_symbol,
            &domain::RankedEtf::symbol
        );
        if (rank == scan.rankings.end()) {
            previous_trade_bar = trade_bar;
            continue;
        }
        const auto current_cycle_phase = cycle_phase(settings_, scan, *rank);
        const bool new_cycle_trend_bar =
            settings_.lifecycle_mode == TradeLifecycleMode::trend_cycle
            && (!last_cycle_trend_timestamp.has_value()
                || scan.epoch_seconds != *last_cycle_trend_timestamp);
        if (new_cycle_trend_bar) {
            last_cycle_trend_timestamp = scan.epoch_seconds;
            if (current_cycle_phase == CyclePhase::weak) {
                // A full bearish reset is what arms another bullish cycle. A
                // brief movement of TQQQ out of its entry zone is not enough.
                entry_armed = true;
                cycle_building_seen = false;
            } else if (current_cycle_phase == CyclePhase::building) {
                cycle_building_seen = true;
            }
        }

        if (position.has_value()) {
            position->peak_price = std::max(position->peak_price, trade_bar.high);
            position->trough_price = std::min(position->trough_price, trade_bar.low);

            const double current_atr = rank->leveraged_entry_zone.has_value()
                ? rank->leveraged_entry_zone->atr14
                : position->trade_atr;
            if (settings_.lifecycle_mode == TradeLifecycleMode::responsive
                && position->peak_price - position->entry_price
                    >= settings_.trailing_activation_atr * position->trade_atr) {
                position->trailing_active = true;
                position->stop_price = std::max(
                    position->stop_price,
                    position->peak_price - settings_.trailing_distance_atr * current_atr
                );
            }

            const auto position_snapshot = simulated_position_snapshot(
                *position,
                trade_bar.close
            );
            const auto execution = historical_execution(
                settings_,
                scan,
                *rank,
                &position_snapshot
            );
            const auto signal_only_execution = historical_execution(
                settings_,
                scan,
                *rank,
                nullptr
            );
            const bool profit_protection_tightened = execution.if_held
                != signal_only_execution.if_held;

            if (settings_.forced_exit_signal_minute.has_value()
                && minute >= *settings_.forced_exit_signal_minute) {
                pending_exit = PendingExit{.reason = ExitReason::session_end};
            } else if (settings_.lifecycle_mode == TradeLifecycleMode::trend_cycle) {
                // Only a newly completed five-minute trend bar may advance the
                // deterioration counter. Re-reading the same trend state on
                // each one-minute execution bar must not force a two-minute exit.
                if (new_cycle_trend_bar
                    && current_cycle_phase == CyclePhase::weak) {
                    pending_exit = PendingExit{.reason = ExitReason::weak_signal};
                } else if (new_cycle_trend_bar
                           && current_cycle_phase == CyclePhase::fading) {
                    ++position->non_strong_bars;
                    if (position->non_strong_bars >= 2) {
                        pending_exit = PendingExit{
                            .reason = ExitReason::momentum_faded,
                        };
                    }
                } else if (new_cycle_trend_bar
                           && (current_cycle_phase == CyclePhase::building
                               || current_cycle_phase == CyclePhase::strong)) {
                    position->non_strong_bars = 0;
                }
            } else if (profit_protection_tightened
                       && (execution.if_held == domain::HoldingGuidance::trim
                           || execution.if_held == domain::HoldingGuidance::exit)) {
                pending_exit = PendingExit{.reason = ExitReason::profit_giveback};
            } else if (signal_only_execution.if_held
                       == domain::HoldingGuidance::exit) {
                pending_exit = PendingExit{.reason = ExitReason::weak_signal};
            } else if (signal_only_execution.if_held
                       == domain::HoldingGuidance::hold) {
                position->non_strong_bars = 0;
            } else {
                ++position->non_strong_bars;
                if (position->non_strong_bars >= 2) {
                    pending_exit = PendingExit{.reason = ExitReason::momentum_faded};
                }
            }
        } else if (in_test_window && !pending_entry.has_value()
                   && (!settings_.entry_start_minute.has_value()
                       || minute >= *settings_.entry_start_minute)
                   && (!settings_.entry_end_minute.has_value()
                       || minute <= *settings_.entry_end_minute)
                   && rank->entry_zone.has_value()
                   && rank->leveraged_entry_zone.has_value()) {
            const auto execution = historical_execution(
                settings_,
                scan,
                *rank,
                nullptr
            );
            const bool trend_cycle_entry =
                settings_.lifecycle_mode == TradeLifecycleMode::trend_cycle
                && entry_armed && cycle_building_seen
                && (current_cycle_phase == CyclePhase::building
                    || current_cycle_phase == CyclePhase::strong)
                && execution.entry == domain::LongEntryDecision::ready;
            if (settings_.lifecycle_mode == TradeLifecycleMode::responsive
                && !entry_armed
                && execution.entry != domain::LongEntryDecision::ready) {
                entry_armed = true;
            } else if (trend_cycle_entry
                       || (settings_.lifecycle_mode == TradeLifecycleMode::responsive
                           && entry_armed
                           && execution.entry == domain::LongEntryDecision::ready)) {
                pending_entry = PendingEntry{
                    .session_date = session,
                    .market_regime = scan.market_regime,
                    .signal_atr = rank->entry_zone->atr14,
                    .trade_atr = rank->leveraged_entry_zone->atr14,
                    .signal_atr_expansion_ratio =
                        rank->entry_zone->atr_expansion_ratio,
                    .trade_atr_expansion_ratio =
                        rank->leveraged_entry_zone->atr_expansion_ratio,
                    .suggested_entry_lower =
                        rank->leveraged_entry_zone->lower_price,
                    .suggested_entry_upper =
                        rank->leveraged_entry_zone->upper_price,
                    .entry_vwap = rank->leveraged_entry_zone->session_vwap,
                };
            }
        }

        previous_trade_bar = trade_bar;
    }

    if (position.has_value() && previous_trade_bar.has_value()) {
        append_trade(
            report,
            settings_,
            *position,
            previous_trade_bar->epoch_seconds,
            previous_trade_bar->close,
            ExitReason::data_end
        );
    }

    report.sessions = sessions.size();
    if (!sessions.empty()) {
        report.first_session = *sessions.begin();
        report.last_session = *sessions.rbegin();
    }
    evaluate_trade_diagnostics(
        report,
        require_instrument(instruments, settings_.trade_symbol).bars
    );
    summarize(report);
    return report;
}

} // namespace daytrader::backtest
