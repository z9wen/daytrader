#include "daytrader/backtest/DayTradeBacktester.hpp"

#include "daytrader/analysis/AnalysisParameters.hpp"
#include "daytrader/analysis/LeveragedExecutionAnalyzer.hpp"
#include "daytrader/analysis/MarketScanner.hpp"
#include "daytrader/config/MarketDataSettings.hpp"
#include "daytrader/domain/TradeDecision.hpp"
#include "daytrader/time/TimeZoneFormatter.hpp"
#include "daytrader/universe/EtfDefinition.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace daytrader::backtest {
namespace {

constexpr std::size_t rolling_analysis_bars = 128;

using BarIndex = std::unordered_map<std::int64_t, const domain::MarketBar*>;

struct PendingEntry {
    std::string session_date;
    domain::MarketRegime market_regime{domain::MarketRegime::neutral};
    double signal_atr{};
    double trade_atr{};
    double signal_atr_expansion_ratio{};
    double trade_atr_expansion_ratio{};
};

struct Position {
    std::string session_date;
    domain::MarketRegime market_regime_at_entry{domain::MarketRegime::neutral};
    std::int64_t entry_timestamp{};
    double signal_entry_price{};
    double entry_price{};
    double signal_atr{};
    double trade_atr{};
    double signal_atr_expansion_ratio{};
    double trade_atr_expansion_ratio{};
    double stop_price{};
    double peak_price{};
    double trough_price{};
    bool trailing_active{};
    int non_strong_bars{};
};

struct PendingExit {
    ExitReason reason{ExitReason::momentum_faded};
};

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

[[nodiscard]] std::vector<universe::EtfDefinition> backtest_universe(
    const DayTradeBacktestSettings& settings
)
{
    using universe::EtfDefinition;
    using universe::EtfGroup;
    return {
        EtfDefinition{
            .market_data = config::HistoricalDataSettings{.symbol = "SPY"},
            .name = "S&P 500",
            .group = EtfGroup::broad_market,
        },
        EtfDefinition{
            .market_data = config::HistoricalDataSettings{.symbol = "QQQ"},
            .name = "Nasdaq-100",
            .group = EtfGroup::broad_market,
            .benchmark_symbol = "SPY",
        },
        EtfDefinition{
            .market_data = config::HistoricalDataSettings{.symbol = settings.signal_symbol},
            .name = settings.signal_symbol,
            .group = EtfGroup::industry,
            .benchmark_symbol = "SPY",
            .leveraged_long_symbol = settings.trade_symbol,
        },
    };
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

[[nodiscard]] domain::BullishPhase historical_signal_phase(
    const DayTradeBacktestSettings& settings,
    const domain::MarketScan& scan,
    const domain::RankedEtf& rank
)
{
    if (settings.signal_symbol != "QQQ") {
        return rank.long_opportunity.phase;
    }
    // QQQ execution is driven by its absolute market snapshot, so its re-arm
    // state must use the same source instead of the duplicate ranking row.
    switch (scan.qqq.trend_signal) {
    case domain::MarketTrendSignal::strong:
        return domain::BullishPhase::strong;
    case domain::MarketTrendSignal::neutral:
        return domain::BullishPhase::building;
    case domain::MarketTrendSignal::weak:
        return domain::BullishPhase::weak;
    }
    return domain::BullishPhase::neutral;
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
        .entry_timestamp = position.entry_timestamp,
        .exit_timestamp = exit_timestamp,
        .signal_entry_price = position.signal_entry_price,
        .entry_price = position.entry_price,
        .exit_price = exit_price,
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
}

} // namespace

DayTradeBacktester::DayTradeBacktester(DayTradeBacktestSettings settings)
    : settings_{std::move(settings)}
{
    if (settings_.strategy_name.empty()) {
        throw std::invalid_argument("backtest strategy name cannot be empty");
    }
    if (settings_.entry_start_minute >= settings_.entry_end_minute) {
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

    std::vector<domain::InstrumentBars> rolling;
    rolling.reserve(symbols.size());
    for (const auto& symbol : symbols) {
        rolling.push_back(domain::InstrumentBars{.symbol = symbol});
    }

    const auto signal_index = static_cast<std::size_t>(
        std::ranges::find(symbols, settings_.signal_symbol) - symbols.begin()
    );
    const auto trade_index = static_cast<std::size_t>(
        std::ranges::find(symbols, settings_.trade_symbol) - symbols.begin()
    );

    BacktestReport report{
        .strategy_name = settings_.strategy_name,
        .signal_symbol = settings_.signal_symbol,
        .trade_symbol = settings_.trade_symbol,
    };
    const auto etfs = backtest_universe(settings_);
    const analysis::MarketScanner scanner{
        settings_.time_zone,
        std::chrono::minutes{5},
    };
    const time::TimeZoneFormatter formatter{settings_.time_zone};
    std::set<std::string> sessions;
    std::optional<PendingEntry> pending_entry;
    std::optional<PendingExit> pending_exit;
    std::optional<Position> position;
    std::optional<domain::MarketBar> previous_trade_bar;
    std::string current_session;
    // The first setup of a session is eligible immediately. After a fill, a
    // fresh BUILDING phase must appear before another STRONG setup can enter;
    // this permits multiple distinct waves without churning inside one wave.
    bool entry_armed{true};

    for (const auto timestamp : timestamps) {
        for (std::size_t index = 0; index < indexes.size(); ++index) {
            rolling[index].bars.push_back(*indexes[index].at(timestamp));
            if (rolling[index].bars.size() > rolling_analysis_bars) {
                rolling[index].bars.erase(rolling[index].bars.begin());
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
                    .entry_timestamp = timestamp,
                    .signal_entry_price = signal_bar.open,
                    .entry_price = trade_bar.open,
                    .signal_atr = pending_entry->signal_atr,
                    .trade_atr = pending_entry->trade_atr,
                    .signal_atr_expansion_ratio =
                        pending_entry->signal_atr_expansion_ratio,
                    .trade_atr_expansion_ratio =
                        pending_entry->trade_atr_expansion_ratio,
                    .stop_price = trade_bar.open
                        - settings_.initial_stop_atr * pending_entry->trade_atr,
                    .peak_price = trade_bar.open,
                    .trough_price = trade_bar.open,
                };
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

        if (rolling.front().bars.size() < analysis::minimum_analysis_bars) {
            previous_trade_bar = trade_bar;
            continue;
        }

        const auto scan = scanner.scan(rolling, etfs);
        const auto rank = std::ranges::find(
            scan.rankings,
            settings_.signal_symbol,
            &domain::RankedEtf::symbol
        );
        if (rank == scan.rankings.end()) {
            previous_trade_bar = trade_bar;
            continue;
        }

        if (position.has_value()) {
            position->peak_price = std::max(position->peak_price, trade_bar.high);
            position->trough_price = std::min(position->trough_price, trade_bar.low);

            const double current_atr = rank->leveraged_entry_zone.has_value()
                ? rank->leveraged_entry_zone->atr14
                : position->trade_atr;
            if (position->peak_price - position->entry_price
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

            if (minute >= settings_.forced_exit_signal_minute) {
                pending_exit = PendingExit{.reason = ExitReason::session_end};
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
                   && minute >= settings_.entry_start_minute
                   && minute <= settings_.entry_end_minute
                   && rank->entry_zone.has_value()
                   && rank->leveraged_entry_zone.has_value()) {
            if (!entry_armed
                && historical_signal_phase(settings_, scan, *rank)
                    == domain::BullishPhase::building) {
                entry_armed = true;
            }
            const auto execution = historical_execution(
                settings_,
                scan,
                *rank,
                nullptr
            );
            if (entry_armed
                && execution.entry == domain::LongEntryDecision::ready) {
                pending_entry = PendingEntry{
                    .session_date = session,
                    .market_regime = scan.market_regime,
                    .signal_atr = rank->entry_zone->atr14,
                    .trade_atr = rank->leveraged_entry_zone->atr14,
                    .signal_atr_expansion_ratio =
                        rank->entry_zone->atr_expansion_ratio,
                    .trade_atr_expansion_ratio =
                        rank->leveraged_entry_zone->atr_expansion_ratio,
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
    summarize(report);
    return report;
}

} // namespace daytrader::backtest
