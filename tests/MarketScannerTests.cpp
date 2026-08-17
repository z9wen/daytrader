#include "daytrader/analysis/MarketScanner.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using daytrader::config::HistoricalDataSettings;
using daytrader::domain::InstrumentBars;
using daytrader::domain::MarketBar;
using daytrader::universe::EtfDefinition;
using daytrader::universe::EtfGroup;

[[nodiscard]] EtfDefinition etf(
    std::string symbol,
    EtfGroup group,
    std::string benchmark,
    std::string leveraged_long = {},
    std::string leveraged_short = {}
)
{
    return EtfDefinition{
        .market_data = HistoricalDataSettings{.symbol = symbol},
        .name = symbol,
        .group = group,
        .benchmark_symbol = std::move(benchmark),
        .leveraged_long_symbol = std::move(leveraged_long),
        .leveraged_short_symbol = std::move(leveraged_short),
    };
}

[[nodiscard]] std::vector<EtfDefinition> test_universe()
{
    return {
        etf("SPY", EtfGroup::broad_market, ""),
        etf("QQQ", EtfGroup::broad_market, "SPY", "TQQQ", "SQQQ"),
        etf("XLK", EtfGroup::sector, "SPY", "TECL", "TECS"),
        etf("XBI", EtfGroup::industry, "SPY", "LABU", "LABD"),
        etf("SOXX", EtfGroup::industry, "SPY", "SOXL", "SOXS"),
    };
}

[[nodiscard]] InstrumentBars make_bars(
    std::string symbol,
    double initial_price,
    double multiplier
)
{
    constexpr std::int64_t first_timestamp = 1'700'000'000;
    InstrumentBars result{.symbol = std::move(symbol)};
    result.bars.reserve(40);

    for (std::size_t index = 0; index < 40; ++index) {
        const double close = initial_price * std::pow(multiplier, static_cast<double>(index));
        result.bars.push_back(MarketBar{
            .epoch_seconds = first_timestamp + static_cast<std::int64_t>(index * 300),
            .open = close * 0.999,
            .high = close * 1.001,
            .low = close * 0.998,
            .close = close,
            .volume = 1'000.0,
            .weighted_average_price = close * 0.9995,
            .trade_count = 100,
        });
    }
    return result;
}

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void bullish_market_selects_strongest_leveraged_industry()
{
    auto instruments = std::vector<InstrumentBars>{
        make_bars("SPY", 100.0, 1.0005),
        make_bars("QQQ", 200.0, 1.0010),
        make_bars("VIX", 16.0, 1.0010),
        make_bars("XLK", 180.0, 1.0020),
        make_bars("XBI", 150.0, 1.0015),
        make_bars("SOXX", 300.0, 1.0030),
        make_bars("TECL", 100.0, 1.0025),
        make_bars("SOXL", 120.0, 1.0035),
    };
    instruments[1].bars.erase(instruments[1].bars.begin() + 5);

    const auto scan = daytrader::analysis::MarketScanner{"America/New_York"}.scan(
        instruments,
        test_universe()
    );

    require(
        scan.market_regime == daytrader::domain::MarketRegime::bullish,
        "expected a bullish market"
    );
    require(
        scan.spy.trend_signal == daytrader::domain::MarketTrendSignal::strong,
        "expected a strong SPY trend signal"
    );
    require(
        scan.qqq.trend_signal == daytrader::domain::MarketTrendSignal::strong,
        "expected a strong QQQ trend signal"
    );
    require(scan.aligned_market_bar_count == 39, "expected timestamp intersection");
    require(scan.vix.has_value(), "expected an optional VIX snapshot");
    require(
        scan.vix->trend == daytrader::domain::VolatilityTrend::rising,
        "expected rising VIX risk context"
    );
    require(scan.sector_rankings.size() == 1, "expected one sector ETF to be ranked");
    require(scan.sector_rankings.front().symbol == "XLK", "expected XLK sector rank");
    require(scan.rankings.size() == 2, "expected only industry ETFs to be ranked");
    require(scan.rankings.front().symbol == "SOXX", "expected SOXX to rank first");
    require(scan.rankings.front().entry_zone.has_value(), "expected SOXX entry zone");
    require(
        scan.rankings.front().entry_zone->symbol == "SOXX",
        "base entry zone must use the signal ETF"
    );
    require(
        scan.rankings.front().leveraged_entry_zone.has_value()
            && scan.rankings.front().leveraged_entry_zone->symbol == "SOXL",
        "expected a separate SOXL leveraged entry zone"
    );
    require(
        scan.sector_rankings.front().leveraged_entry_zone.has_value()
            && scan.sector_rankings.front().leveraged_entry_zone->symbol == "TECL",
        "expected a separate TECL leveraged entry zone"
    );
    require(scan.candidate.has_value(), "expected a leveraged candidate");
    require(scan.candidate->trade_symbol == "SOXL", "expected SOXL candidate");
    require(scan.sector_candidate.has_value(), "expected a leveraged sector candidate");
    require(scan.sector_candidate->trade_symbol == "TECL", "expected TECL candidate");
}

void bearish_market_selects_weakest_inverse_industry()
{
    const auto instruments = std::vector<InstrumentBars>{
        make_bars("SPY", 100.0, 0.9995),
        make_bars("QQQ", 200.0, 0.9990),
        make_bars("XLK", 180.0, 0.9980),
        make_bars("XBI", 150.0, 0.9985),
        make_bars("SOXX", 300.0, 0.9970),
        make_bars("SOXL", 120.0, 0.9965),
    };

    const auto scan = daytrader::analysis::MarketScanner{"America/New_York"}.scan(
        instruments,
        test_universe()
    );

    require(
        scan.market_regime == daytrader::domain::MarketRegime::bearish,
        "expected a bearish market"
    );
    require(
        scan.spy.trend_signal == daytrader::domain::MarketTrendSignal::weak,
        "expected a weak SPY trend signal"
    );
    require(
        scan.qqq.trend_signal == daytrader::domain::MarketTrendSignal::weak,
        "expected a weak QQQ trend signal"
    );
    require(scan.rankings.back().symbol == "SOXX", "expected SOXX to rank last");
    require(scan.rankings.back().entry_zone.has_value(), "expected SOXX base entry zone");
    require(scan.rankings.back().leveraged_entry_zone.has_value(), "expected SOXL entry zone");
    require(
        scan.rankings.back().leveraged_entry_zone->symbol == "SOXL",
        "weak industry signals should still display the long-leveraged ETF zone"
    );
    require(
        scan.rankings.back().leveraged_entry_zone->state
            == daytrader::domain::EntryZoneState::trend_unconfirmed,
        "weak long-leveraged ETF should be marked NO_TREND"
    );
    require(scan.candidate.has_value(), "expected an inverse candidate");
    require(scan.candidate->trade_symbol == "SOXS", "expected SOXS candidate");
    require(scan.sector_candidate.has_value(), "expected an inverse sector candidate");
    require(scan.sector_candidate->trade_symbol == "TECS", "expected TECS candidate");
}

} // namespace

int main()
{
    try {
        bullish_market_selects_strongest_leveraged_industry();
        bearish_market_selects_weakest_inverse_industry();
        std::cout << "MarketScannerTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "MarketScannerTests failed: " << exception.what() << '\n';
        return 1;
    }
}
