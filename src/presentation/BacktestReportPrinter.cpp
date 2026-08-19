#include "daytrader/presentation/BacktestReportPrinter.hpp"

#include "daytrader/time/TimeZoneFormatter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace daytrader::presentation {
namespace {

struct SubsetStats {
    std::size_t trades{};
    std::size_t wins{};
    double net_return_sum{};
};

[[nodiscard]] double win_rate(const SubsetStats& stats)
{
    return stats.trades == 0
        ? 0.0
        : static_cast<double>(stats.wins) / static_cast<double>(stats.trades) * 100.0;
}

[[nodiscard]] std::size_t market_index(domain::MarketRegime regime)
{
    switch (regime) {
    case domain::MarketRegime::bullish:
        return 0;
    case domain::MarketRegime::neutral:
        return 1;
    case domain::MarketRegime::bearish:
        return 2;
    }
    return 1;
}

[[nodiscard]] std::size_t entry_period_index(int minute)
{
    if (minute < 9 * 60 + 30) {
        return 0;
    }
    if (minute < 9 * 60 + 45) {
        return 1;
    }
    if (minute < 11 * 60 + 30) {
        return 2;
    }
    if (minute < 14 * 60) {
        return 3;
    }
    if (minute < 16 * 60) {
        return 4;
    }
    return 5;
}

void print_subset(
    std::ostringstream& output,
    std::string_view label,
    const SubsetStats& stats
)
{
    output << label << ' ' << stats.trades;
    if (stats.trades > 0) {
        output << " (" << std::fixed << std::setprecision(1)
               << win_rate(stats) << "% win, " << std::setprecision(3)
               << stats.net_return_sum / static_cast<double>(stats.trades)
               << "% avg)";
    }
}

} // namespace

BacktestReportPrinter::BacktestReportPrinter(std::string time_zone)
    : time_zone_{std::move(time_zone)}
{
}

std::string BacktestReportPrinter::render(
    std::span<const backtest::BacktestReport> reports
) const
{
    std::ostringstream output;
    output << "\nETF -> LEVERAGED ETF INTRADAY BACKTEST\n"
           << "Direction comes from QQQ/SOXX; execution timing comes from "
              "TQQQ/SOXL's own VWAP/ATR zone.\n"
           << "No hard SPY/QQQ market gate; five-minute trend context is executed "
              "from completed one-minute bars at the next bar open.\n"
           << "All cached IBKR sessions are eligible, including premarket and "
              "after-hours; no daily trade-count cap; 2 bps cost per side.\n"
           << "Order Flow is NOT replayed because full-session historical ticks "
              "are not cached.\n\n";
    output << std::left << std::setw(21) << "strategy"
           << std::right << std::setw(10) << "sessions"
           << std::setw(9) << "trades"
           << std::setw(10) << "win %"
           << std::setw(11) << "avg net %"
           << std::setw(12) << "compound %"
           << std::setw(10) << "PF"
           << std::setw(10) << "max DD %"
           << std::setw(11) << "avg mins" << '\n';

    for (const auto& report : reports) {
        output << std::left << std::setw(21) << report.strategy_name
               << std::right << std::setw(10) << report.sessions
               << std::setw(9) << report.trades
               << std::setw(10) << std::fixed << std::setprecision(1)
               << report.win_rate_percent
               << std::setw(11) << std::setprecision(3)
               << report.average_net_return_percent
               << std::setw(12) << report.compounded_return_percent;
        if (std::isfinite(report.profit_factor)) {
            output << std::setw(10) << std::setprecision(2) << report.profit_factor;
        } else {
            output << std::setw(10) << "INF";
        }
        output << std::setw(10) << std::setprecision(2)
               << report.maximum_drawdown_percent
               << std::setw(11) << std::setprecision(1)
               << report.average_holding_minutes << '\n';
        output << "  period " << report.first_session << " to " << report.last_session
               << " | trade days " << report.traded_sessions
               << " | max/day " << report.maximum_trades_in_session
               << " | wins " << report.wins << " | losses " << report.losses
               << " | avg win " << std::setprecision(3) << report.average_win_percent
               << "% | avg loss " << report.average_loss_percent
               << "% | avg MFE " << report.average_mfe_percent << "%\n";
    }

    const time::TimeZoneFormatter formatter{time_zone_};
    for (const auto& report : reports) {
        std::array<SubsetStats, 3> markets{};
        std::array<SubsetStats, 6> periods{};
        std::array<SubsetStats, 2> trade_order{};
        for (const auto& trade : report.trade_log) {
            auto& market = markets[market_index(trade.market_regime_at_entry)];
            auto& period = periods[entry_period_index(
                formatter.minutes_since_midnight(trade.entry_timestamp)
            )];
            auto& order = trade_order[trade.trade_number_in_session == 1 ? 0 : 1];
            ++market.trades;
            ++period.trades;
            ++order.trades;
            market.net_return_sum += trade.net_return_percent;
            period.net_return_sum += trade.net_return_percent;
            order.net_return_sum += trade.net_return_percent;
            if (trade.net_return_percent > 0.0) {
                ++market.wins;
                ++period.wins;
                ++order.wins;
            }
        }
        output << "  " << report.strategy_name << " market context: ";
        print_subset(output, "BULLISH", markets[0]);
        output << " | ";
        print_subset(output, "NEUTRAL", markets[1]);
        output << " | ";
        print_subset(output, "BEARISH", markets[2]);
        output << '\n';
        output << "  " << report.strategy_name << " entry time: ";
        print_subset(output, "PREMARKET", periods[0]);
        output << " | ";
        print_subset(output, "OPEN", periods[1]);
        output << " | ";
        print_subset(output, "MORNING", periods[2]);
        output << " | ";
        print_subset(output, "MIDDAY", periods[3]);
        output << " | ";
        print_subset(output, "AFTERNOON", periods[4]);
        output << " | ";
        print_subset(output, "AFTER_HOURS", periods[5]);
        output << '\n';
        output << "  " << report.strategy_name << " trade order: ";
        print_subset(output, "PRIMARY", trade_order[0]);
        output << " | ";
        print_subset(output, "REENTRIES", trade_order[1]);
        output << '\n';
    }

    for (const auto& report : reports) {
        output << "\nLAST TRADES: " << report.strategy_name << '\n';
        if (report.trade_log.empty()) {
            output << "  No qualifying trades.\n";
            continue;
        }
        output << std::left << std::setw(12) << "date"
               << std::setw(4) << "#"
               << std::setw(25) << "entry"
               << std::setw(25) << "exit"
               << std::setw(10) << "market"
               << std::right << std::setw(10) << "buy"
               << std::setw(10) << "sell"
               << std::setw(10) << "net %"
               << std::setw(18) << "reason" << '\n';
        const std::size_t first = report.trade_log.size() > 10
            ? report.trade_log.size() - 10
            : 0;
        for (const auto& trade : std::span{report.trade_log}.subspan(first)) {
            output << std::left << std::setw(12) << trade.session_date
                   << std::setw(4) << trade.trade_number_in_session
                   << std::setw(25) << formatter.format(trade.entry_timestamp)
                   << std::setw(25) << formatter.format(trade.exit_timestamp)
                   << std::setw(10) << domain::to_string(
                          trade.market_regime_at_entry
                      )
                   << std::right << std::setw(10) << std::fixed << std::setprecision(2)
                   << trade.entry_price
                   << std::setw(10) << trade.exit_price
                   << std::setw(10) << std::setprecision(3) << trade.net_return_percent
                   << std::setw(18) << backtest::to_string(trade.exit_reason) << '\n';
        }
    }

    output << "\nExploratory result only: this is an in-sample rule evaluation, not a promise "
              "of future win rate.\n";
    return output.str();
}

} // namespace daytrader::presentation
