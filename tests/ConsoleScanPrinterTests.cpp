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
            .current_price = 149.75,
            .state = daytrader::domain::EntryZoneState::extended,
        },
        .long_opportunity = daytrader::domain::LongOpportunity{
            .bullish_score = 80,
            .phase = daytrader::domain::BullishPhase::strong,
            .entry = daytrader::domain::LongEntryDecision::ready,
            .if_held = daytrader::domain::HoldingGuidance::hold,
        },
        .leveraged_execution = daytrader::domain::LeveragedExecutionDecision{
            .entry = daytrader::domain::LongEntryDecision::wait_for_flow,
            .if_held = daytrader::domain::HoldingGuidance::trim,
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
        .tqqq = daytrader::domain::EtfSnapshot{
            .symbol = "TQQQ",
            .close = 80.0,
            .session_vwap = 79.5,
            .vwap_structure = daytrader::domain::VwapStructureState::above_rising,
            .relative_volume = daytrader::domain::RelativeVolumeSnapshot{
                .bar_ratio = 1.3,
                .cumulative_ratio = 1.1,
                .baseline_sessions = 20,
                .state = daytrader::domain::RelativeVolumeState::expanding,
            },
            .trend_signal = daytrader::domain::MarketTrendSignal::strong,
        },
        .tqqq_entry_zone = daytrader::domain::EntryZone{
            .symbol = "TQQQ",
            .lower_price = 79.4,
            .upper_price = 79.6,
            .state = daytrader::domain::EntryZoneState::extended,
        },
        .tqqq_execution = daytrader::domain::LeveragedExecutionDecision{
            .entry = daytrader::domain::LongEntryDecision::wait_for_flow,
            .if_held = daytrader::domain::HoldingGuidance::protect,
        },
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
            .unrealized_pnl = 180.0,
            .peak_unrealized_pnl = 300.0,
            .giveback_amount = 120.0,
            .giveback_percent = 40.0,
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
        .quotes = {daytrader::domain::LiveQuoteSnapshot{
            .symbol = "SOXL",
            .feed_type = daytrader::domain::MarketDataFeedType::live,
            .selected_price = 52.0,
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
    require(contains(market, "MARKET DIRECTION"), "market tab should show direction ETFs");
    require(contains(market, "TQQQ EXECUTION"), "market tab should show TQQQ execution");
    require(contains(market, "79.40-79.60"), "market tab should show the TQQQ entry zone");
    require(contains(market, "WAIT_FLOW"),
            "market tab should show the QQQ-flow-gated TQQQ decision");
    require(contains(market, "VIX RISK REFERENCE"), "market tab should show VIX context");
    require(contains(market, "RISING"), "market tab should show the VIX trend");
    require(!contains(market, "SECTOR ROTATION"), "market tab should not show sectors");

    const auto trade = printer.render(scan, DashboardTab::trade);
    require(contains(trade, "LIVE TRADE CONTEXT"), "trade tab should show live context");
    require(contains(trade, "SOXL"), "trade tab should show open positions");
    require(contains(trade, "BUY_EFFECTIVE"), "trade tab should show flow pressure");
    require(contains(trade, "LIVE=1"),
            "trade tab should display IBKR's actual market-data callback mode");
    require(contains(trade, "TRIM"),
            "trade tab should show MFE-aware position guidance");
    require(!contains(trade, "SECTOR ROTATION"), "trade tab should remain independent");

    const auto sectors = printer.render(scan, DashboardTab::sectors);
    require(contains(sectors, "SECTOR ROTATION"), "sector tab should show sectors");
    require(contains(sectors, "XLK"), "sector tab should contain XLK");
    require(!contains(sectors, "INDUSTRY ROTATION"), "sector tab should not show industries");

    const auto industries = printer.render(scan, DashboardTab::industries);
    require(contains(industries, "INDUSTRY ROTATION"), "industry tab should show industries");
    require(contains(industries, "SOXX"), "industry tab should contain SOXX");
    require(!contains(industries, "leveraged entry zone"),
            "industry tab should leave leveraged execution columns to tab 4");
    const auto entry_zone = industries.find("entry zone");
    const auto entry_state = industries.find("entry state", entry_zone);
    require(entry_zone < entry_state,
            "industry entry zone and state should retain their requested order");

    const auto leveraged = printer.render(scan, DashboardTab::leveraged);
    require(contains(leveraged, "LEVERAGED ETF WATCHLIST"),
            "leveraged tab should have its own watchlist title");
    require(contains(leveraged, "SOXX"),
            "leveraged tab should identify the signal ETF");
    require(contains(leveraged, "SOXL"),
            "leveraged tab should identify the long leveraged ETF");
    require(contains(leveraged, "149.00-150.00"),
            "leveraged tab should show the leveraged entry zone");
    require(contains(leveraged, "149.75"),
            "leveraged tab should show the leveraged current price");
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
        DashboardViewport{.columns = 200, .rows = 20, .requested_page = 0}
    );
    require(contains(regular.text, "entry zone"),
            "regular layout should retain the entry-zone column");
    require(contains(regular.text, "entry state"),
            "regular layout should retain the entry-state column");
    require(!contains(regular.text, "|entry"),
            "responsive headers should not attach a separator to entry state");
    require(contains(regular.text, "15 S/Q"),
            "regular rows should expose SPY/QQQ multi-period RS pairs");

    const auto focused = printer.render_page(
        sample_scan(),
        DashboardTab::industries,
        DashboardViewport{.columns = 163, .rows = 20, .requested_page = 0}
    );
    require(contains(focused.text, "entry/held"),
            "common wide terminals should show the focused action column");
    require(contains(focused.text, "30 S/Q"),
            "the industry view should restore 30-minute RS after removing leverage columns");
    require(contains(focused.text, "symbol  price       RVOL"),
            "comfortable layout should visibly separate adjacent columns");
    require(contains(focused.text, "100.00"),
            "industry rows should show current price before RVOL");
    require(contains(focused.text, "60 S/Q       entry zone"),
            "comfortable layout should separate signal and execution groups");
    require(contains(focused.text, "IN_ZONE"),
            "comfortable layout should retain the complete entry state");
    require(!contains(focused.text, "leveraged zone"),
            "industry layout should no longer include leveraged columns");

    const auto leveraged = printer.render_page(
        sample_scan(),
        DashboardTab::leveraged,
        DashboardViewport{.columns = 163, .rows = 20, .requested_page = 0}
    );
    require(contains(leveraged.text, "LEVERAGED ETF WATCHLIST"),
            "responsive tab 4 should render the leveraged watchlist");
    require(contains(leveraged.text, "SOXX") && contains(leveraged.text, "SOXL"),
            "leveraged rows should preserve signal-to-long mappings");
    require(contains(leveraged.text, "EXTENDED"),
            "leveraged layout should retain the complete entry state");
    require(contains(leveraged.text, "FLOW/TRIM"),
            "leveraged layout should show execution and MFE guidance");

    const auto sectors = printer.render_page(
        sample_scan(),
        DashboardTab::sectors,
        DashboardViewport{.columns = 170, .rows = 20, .requested_page = 0}
    );
    require(contains(sectors.text, "symbol  price       RVOL"),
            "sector rows should show current price before RVOL");
    require(contains(sectors.text, "100.00"),
            "sector rows should include the current ETF price");
    require(contains(sectors.text, "FLOW/TRIM"),
            "sector action should use the leveraged ETF execution decision");

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
