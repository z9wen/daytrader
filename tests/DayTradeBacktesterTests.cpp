#include "daytrader/backtest/DayTradeBacktester.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::int64_t session_open = 1'704'205'800;

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] daytrader::domain::InstrumentBars make_session(
    std::string symbol,
    double initial_price,
    double increment,
    double half_range
)
{
    // 2024-01-02 09:30 America/New_York, followed by one complete RTH session.
    daytrader::domain::InstrumentBars result{.symbol = std::move(symbol)};
    for (int index = 0; index < 78; ++index) {
        const double price = initial_price + increment * static_cast<double>(index);
        result.bars.push_back(daytrader::domain::MarketBar{
            .epoch_seconds = session_open + static_cast<std::int64_t>(index * 300),
            .open = price,
            .high = price + half_range,
            .low = price - half_range,
            .close = price,
            .volume = 100'000.0,
            .weighted_average_price = price,
            .trade_count = 1'000,
        });
    }
    return result;
}

void enters_on_next_bar_and_closes_the_same_session()
{
    const std::vector<daytrader::domain::InstrumentBars> instruments{
        make_session("SPY", 100.0, 0.001, 0.10),
        make_session("QQQ", 200.0, 0.003, 0.15),
        make_session("SOXX", 300.0, 0.005, 0.20),
        make_session("SOXL", 50.0, 0.003, 0.10),
    };
    const auto report = daytrader::backtest::DayTradeBacktester{
        daytrader::backtest::DayTradeBacktestSettings{
            .strategy_name = "synthetic",
            .initial_stop_atr = 100.0,
            .trailing_activation_atr = 100.0,
            .per_side_cost_basis_points = 0.0,
        }
    }.run(instruments);

    require(report.sessions == 1, "expected one RTH session");
    require(report.trades == 1, "expected exactly one trade per session");
    require(report.wins == 1, "expected the synthetic rising trade to win");
    require(report.trade_log.front().signal_atr_at_entry > 0.0,
            "the SOXX signal ATR should be retained with the trade");
    require(report.trade_log.front().trade_atr_at_entry > 0.0,
            "the leveraged execution ATR should be retained with the trade");
    require(report.trade_log.front().signal_atr_percent_at_entry > 0.0,
            "signal ATR should also be normalized by SOXX entry price");
    require(report.trade_log.front().signal_atr_expansion_ratio > 0.0,
            "the SOXX fast/slow ATR ratio should be retained with the trade");
    require(
        report.trade_log.front().entry_timestamp > session_open + 20 * 300,
        "entry must occur after the completed signal bar"
    );
    require(
        report.trade_log.front().exit_reason == daytrader::backtest::ExitReason::session_end,
        "day trade must close before the session ends"
    );
}

void excludes_warmup_bars_from_the_report_window()
{
    const std::vector<daytrader::domain::InstrumentBars> instruments{
        make_session("SPY", 100.0, 0.001, 0.10),
        make_session("QQQ", 200.0, 0.003, 0.15),
        make_session("SOXX", 300.0, 0.005, 0.20),
        make_session("SOXL", 50.0, 0.003, 0.10),
    };
    const auto report = daytrader::backtest::DayTradeBacktester{
        daytrader::backtest::DayTradeBacktestSettings{
            .strategy_name = "windowed",
            .earliest_entry_timestamp = session_open + 78 * 300,
        }
    }.run(instruments);

    require(report.sessions == 0, "warmup-only sessions must not enter report statistics");
    require(report.trades == 0, "warmup-only bars must not create trades");
}

} // namespace

int main()
{
    try {
        enters_on_next_bar_and_closes_the_same_session();
        excludes_warmup_bars_from_the_report_window();
        std::cout << "DayTradeBacktesterTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "DayTradeBacktesterTests failed: " << exception.what() << '\n';
        return 1;
    }
}
