#include "daytrader/presentation/BacktestReportPrinter.hpp"

#include "daytrader/time/TimeZoneFormatter.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace daytrader::presentation {

BacktestReportPrinter::BacktestReportPrinter(std::string time_zone)
    : time_zone_{std::move(time_zone)}
{
}

std::string BacktestReportPrinter::render(
    std::span<const backtest::BacktestReport> reports
) const
{
    std::ostringstream output;
    output << "\nSOXX -> SOXL INTRADAY BACKTEST\n"
           << "Signals use completed 5-minute bars; fills occur at the next bar open.\n"
           << "RTH only, one trade per session, 2 bps estimated cost per side.\n\n";
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
               << " | wins " << report.wins << " | losses " << report.losses
               << " | avg win " << std::setprecision(3) << report.average_win_percent
               << "% | avg loss " << report.average_loss_percent
               << "% | avg MFE " << report.average_mfe_percent << "%\n";
    }

    const time::TimeZoneFormatter formatter{time_zone_};
    for (const auto& report : reports) {
        output << "\nLAST TRADES: " << report.strategy_name << '\n';
        if (report.trade_log.empty()) {
            output << "  No qualifying trades.\n";
            continue;
        }
        output << std::left << std::setw(12) << "date"
               << std::setw(25) << "entry"
               << std::setw(25) << "exit"
               << std::right << std::setw(10) << "buy"
               << std::setw(10) << "sell"
               << std::setw(10) << "net %"
               << std::setw(18) << "reason" << '\n';
        const std::size_t first = report.trade_log.size() > 10
            ? report.trade_log.size() - 10
            : 0;
        for (const auto& trade : std::span{report.trade_log}.subspan(first)) {
            output << std::left << std::setw(12) << trade.session_date
                   << std::setw(25) << formatter.format(trade.entry_timestamp)
                   << std::setw(25) << formatter.format(trade.exit_timestamp)
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
