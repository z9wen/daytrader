#include "daytrader/presentation/ConsoleScanPrinter.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] bool contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

[[nodiscard]] daytrader::domain::RankedEtf rank(
    std::string symbol,
    std::string name,
    std::string leveraged_symbol
)
{
    return daytrader::domain::RankedEtf{
        .symbol = std::move(symbol),
        .name = std::move(name),
        .group = "INDUSTRY",
        .benchmark_symbol = "SPY",
        .leveraged_long_symbol = leveraged_symbol,
        .leveraged_short_symbol = "REFERENCE",
        .close = 100.0,
        .session_vwap = 99.5,
        .ema20 = 99.8,
        .ema20_change_percent = 0.1,
        .relative_change_60_min_percent = 0.2,
        .signal = daytrader::domain::RelativeStrengthSignal::strong,
        .entry_zone = daytrader::domain::EntryZone{
            .symbol = "BASE",
            .lower_price = 99.0,
            .upper_price = 100.0,
            .state = daytrader::domain::EntryZoneState::in_zone,
        },
        .leveraged_entry_zone = daytrader::domain::EntryZone{
            .symbol = std::move(leveraged_symbol),
            .lower_price = 149.0,
            .upper_price = 150.0,
            .state = daytrader::domain::EntryZoneState::extended,
        },
    };
}

[[nodiscard]] daytrader::domain::MarketScan sample_scan()
{
    return daytrader::domain::MarketScan{
        .epoch_seconds = 1'700'000'000,
        .aligned_market_bar_count = 40,
        .spy = daytrader::domain::EtfSnapshot{.symbol = "SPY", .close = 500.0},
        .qqq = daytrader::domain::EtfSnapshot{.symbol = "QQQ", .close = 450.0},
        .vix = daytrader::domain::VolatilitySnapshot{
            .close = 18.0,
            .ema20 = 17.5,
            .change_60_min_percent = 3.0,
            .trend = daytrader::domain::VolatilityTrend::rising,
        },
        .market_regime = daytrader::domain::MarketRegime::neutral,
        .sector_rankings = {rank("XLK", "Technology", "TECL")},
        .rankings = {rank("SOXX", "Semiconductors", "SOXL")},
    };
}

void renders_independent_tabs_and_column_order()
{
    using daytrader::presentation::DashboardTab;
    const daytrader::presentation::ConsoleScanPrinter printer{"America/New_York"};
    const auto scan = sample_scan();

    const auto market = printer.render(scan, DashboardTab::market);
    require(contains(market, "MARKET ETFs"), "market tab should show market ETFs");
    require(contains(market, "VIX RISK REFERENCE"), "market tab should show VIX context");
    require(contains(market, "RISING"), "market tab should show the VIX trend");
    require(!contains(market, "SECTOR ROTATION"), "market tab should not show sectors");

    const auto sectors = printer.render(scan, DashboardTab::sectors);
    require(contains(sectors, "SECTOR ROTATION"), "sector tab should show sectors");
    require(contains(sectors, "XLK"), "sector tab should contain XLK");
    require(!contains(sectors, "INDUSTRY ROTATION"), "sector tab should not show industries");

    const auto industries = printer.render(scan, DashboardTab::industries);
    require(contains(industries, "INDUSTRY ROTATION"), "industry tab should show industries");
    require(contains(industries, "SOXX"), "industry tab should contain SOXX");
    const auto entry_zone = industries.find("entry zone");
    const auto entry_state = industries.find("entry state", entry_zone);
    const auto leveraged_zone = industries.find("leveraged entry zone", entry_state);
    const auto leveraged_state = industries.find("leveraged state", leveraged_zone);
    require(
        entry_zone < entry_state && entry_state < leveraged_zone
            && leveraged_zone < leveraged_state,
        "entry and leveraged columns should follow the requested order"
    );
}

} // namespace

int main()
{
    try {
        renders_independent_tabs_and_column_order();
        std::cout << "ConsoleScanPrinterTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ConsoleScanPrinterTests failed: " << exception.what() << '\n';
        return 1;
    }
}
