#include "daytrader/presentation/ConsoleScanPrinter.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

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
        .relative_strength_vs_spy = daytrader::domain::RelativeStrengthHorizons{
            .fifteen_minute_percent = 0.1,
            .thirty_minute_percent = 0.2,
            .sixty_minute_percent = 0.3,
        },
        .relative_strength_vs_qqq = daytrader::domain::RelativeStrengthHorizons{
            .fifteen_minute_percent = 0.0,
            .thirty_minute_percent = 0.1,
            .sixty_minute_percent = 0.2,
        },
        .relative_change_60_min_percent = 0.2,
        .vwap_structure = daytrader::domain::VwapStructureState::above_rising,
        .relative_volume = daytrader::domain::RelativeVolumeSnapshot{
            .bar_ratio = 1.25,
            .cumulative_ratio = 1.10,
            .baseline_sessions = 20,
            .state = daytrader::domain::RelativeVolumeState::expanding,
        },
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
    auto scan = daytrader::domain::MarketScan{
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
    scan.live_context = daytrader::domain::LiveTradeContext{
        .updated_epoch_seconds = 1'700'000'001,
        .positions_ready = true,
        .order_flow_connected = true,
        .positions = {daytrader::domain::PositionSnapshot{
            .account = "DU1",
            .symbol = "SOXL",
            .contract_id = 1,
            .quantity = 100.0,
            .average_cost = 50.0,
            .market_price = 52.0,
            .unrealized_pnl = 200.0,
            .peak_unrealized_pnl = 300.0,
            .giveback_amount = 100.0,
            .giveback_percent = 33.3,
        }},
        .order_flow = {daytrader::domain::LiveOrderFlowSnapshot{
            .symbol = "SOXX",
            .updated_epoch_seconds = 1'700'000'001,
            .assessment = daytrader::domain::OrderFlowAssessment{
                .delta_acceleration_points = 12.0,
                .evidence_quality_percent = 80.0,
                .pressure = daytrader::domain::OrderFlowPressureState::buying_effective,
            },
        }},
    };
    scan.live_context.order_flow[0].thirty_seconds.flow.delta_ratio_percent = 40.0;
    scan.live_context.order_flow[0].one_minute.flow.delta_ratio_percent = 28.0;
    return scan;
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

    const auto trade = printer.render(scan, DashboardTab::trade);
    require(contains(trade, "LIVE TRADE CONTEXT"), "trade tab should show live context");
    require(contains(trade, "SOXL"), "trade tab should show open positions");
    require(contains(trade, "BUY_EFFECTIVE"), "trade tab should show flow pressure");
    require(!contains(trade, "SECTOR ROTATION"), "trade tab should remain independent");

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

void renders_responsive_industry_pages_without_overflow()
{
    using daytrader::presentation::DashboardTab;
    using daytrader::presentation::DashboardViewport;
    const daytrader::presentation::ConsoleScanPrinter printer{"America/New_York"};
    auto scan = sample_scan();
    scan.rankings.clear();
    for (int index = 0; index < 20; ++index) {
        scan.rankings.push_back(rank(
            "I" + std::to_string(index),
            "Industry " + std::to_string(index),
            "L" + std::to_string(index)
        ));
    }

    const auto first = printer.render_page(
        scan,
        DashboardTab::industries,
        DashboardViewport{.columns = 60, .rows = 12, .requested_page = 0}
    );
    require(first.page_count > 1, "industry rows should paginate to terminal height");
    require(first.page_index == 0, "the first responsive page should remain selected");

    std::size_t line_count{};
    std::size_t line_start{};
    while (line_start < first.text.size()) {
        const auto line_end = first.text.find('\n', line_start);
        const auto length = (line_end == std::string::npos ? first.text.size() : line_end)
            - line_start;
        require(length <= 60, "responsive rows must not exceed terminal width");
        ++line_count;
        if (line_end == std::string::npos) {
            break;
        }
        line_start = line_end + 1;
    }
    require(line_count <= 12, "responsive page must fit the terminal height");

    const auto last = printer.render_page(
        scan,
        DashboardTab::industries,
        DashboardViewport{
            .columns = 60,
            .rows = 12,
            .requested_page = first.page_count - 1,
        }
    );
    require(last.page_index == first.page_count - 1,
            "the final responsive page should be reachable");
    require(last.text != first.text, "different pages should show different rows");

    const auto regular = printer.render_page(
        sample_scan(),
        DashboardTab::industries,
        DashboardViewport{.columns = 156, .rows = 20, .requested_page = 0}
    );
    require(contains(regular.text, "entry zone"),
            "regular layout should retain the entry-zone column");
    require(contains(regular.text, "entry state"),
            "regular layout should retain the entry-state column");
    require(!contains(regular.text, "|entry"),
            "responsive headers should not attach a separator to entry state");
    require(contains(regular.text, "15 S/Q"),
            "regular rows should expose SPY/QQQ multi-period RS pairs");

    const auto narrow = printer.render_page(
        sample_scan(),
        DashboardTab::industries,
        DashboardViewport{.columns = 80, .rows = 20, .requested_page = 0}
    );
    require(!contains(narrow.text, "S/Qentry"),
            "minimal layout should separate RS from entry zone");
    require(!contains(narrow.text, "phasescore"),
            "minimal layout should separate phase from score");

    const auto trade = printer.render_page(
        sample_scan(),
        DashboardTab::trade,
        DashboardViewport{.columns = 80, .rows = 24, .requested_page = 0}
    );
    require(contains(trade.text, "peak MFE"),
            "compact trade layout should retain peak profit");
    require(contains(trade.text, "gb%"),
            "compact trade layout should retain giveback percent");
    require(contains(trade.text, "CLOSED"),
            "after-hours order flow should be labeled closed rather than warming");
}

} // namespace

int main()
{
    try {
        renders_independent_tabs_and_column_order();
        renders_responsive_industry_pages_without_overflow();
        std::cout << "ConsoleScanPrinterTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ConsoleScanPrinterTests failed: " << exception.what() << '\n';
        return 1;
    }
}
