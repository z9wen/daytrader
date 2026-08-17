#include "daytrader/presentation/ConsoleScanPrinter.hpp"

#include "daytrader/analysis/RotationGrouper.hpp"
#include "daytrader/time/TimeZoneFormatter.hpp"

#include <algorithm>
#include <iomanip>
#include <optional>
#include <ostream>
#include <sstream>
#include <string_view>
#include <utility>

namespace daytrader::presentation {
namespace {

void print_market_snapshot(std::ostream& output, const domain::EtfSnapshot& snapshot)
{
    output << std::left << std::setw(8) << snapshot.symbol
           << std::right << std::setw(12) << std::fixed << std::setprecision(2)
           << snapshot.close;
    if (snapshot.session_vwap.has_value()) {
        output << std::setw(12) << *snapshot.session_vwap;
    } else {
        output << std::setw(12) << '-';
    }
    output << std::setw(12) << snapshot.ema20
           << std::setw(12) << std::setprecision(4) << snapshot.ema20_change_percent
           << std::setw(10) << domain::to_string(snapshot.trend_signal)
           << '\n';
}

[[nodiscard]] std::string_view symbol_or_dash(const std::string& symbol)
{
    return symbol.empty() ? std::string_view{"-"} : std::string_view{symbol};
}

[[nodiscard]] std::string entry_zone_text(const std::optional<domain::EntryZone>& zone)
{
    if (!zone.has_value()) {
        return "-";
    }
    std::ostringstream output;
    output << zone->symbol << ' ' << std::fixed << std::setprecision(2)
           << zone->lower_price << '-' << zone->upper_price;
    return output.str();
}

[[nodiscard]] std::string_view entry_zone_state(
    const std::optional<domain::EntryZone>& zone
)
{
    return zone.has_value() ? domain::to_string(zone->state) : std::string_view{"-"};
}

void print_rotation_table_header(std::ostream& output)
{
    output << std::left << std::setw(8) << "symbol"
           << std::setw(10) << "vs"
           << std::setw(28) << "name"
           << std::right << std::setw(11) << "close"
           << std::setw(11) << "VWAP"
           << std::setw(10) << "EMA %"
           << std::setw(11) << "RS %"
           << std::setw(24) << "entry zone"
           << std::setw(14) << "entry state"
           << std::setw(24) << "leveraged entry zone"
           << std::setw(16) << "leveraged state"
           << std::setw(11) << "phase"
           << std::setw(8) << "score"
           << std::setw(12) << "entry"
           << std::setw(11) << "if held"
           << std::setw(9) << "long"
           << std::setw(9) << "short" << '\n';
}

void print_rotation_rank(std::ostream& output, const domain::RankedEtf& rank)
{
    output << std::left << std::setw(8) << rank.symbol
           << std::setw(10) << rank.benchmark_symbol
           << std::setw(28) << rank.name
           << std::right << std::setw(11) << std::fixed << std::setprecision(2)
           << rank.close;
    if (rank.session_vwap.has_value()) {
        output << std::setw(11) << *rank.session_vwap;
    } else {
        output << std::setw(11) << '-';
    }
    output << std::setw(10) << std::setprecision(3)
           << rank.ema20_change_percent
           << std::setw(11) << rank.relative_change_60_min_percent
           << std::setw(24) << entry_zone_text(rank.entry_zone)
           << std::setw(14) << entry_zone_state(rank.entry_zone)
           << std::setw(24) << entry_zone_text(rank.leveraged_entry_zone)
           << std::setw(16) << entry_zone_state(rank.leveraged_entry_zone)
           << std::setw(11) << domain::to_string(rank.long_opportunity.phase)
           << std::setw(8) << rank.long_opportunity.bullish_score
           << std::setw(12) << domain::to_string(rank.long_opportunity.entry)
           << std::setw(11) << domain::to_string(rank.long_opportunity.if_held)
           << std::setw(9) << symbol_or_dash(rank.leveraged_long_symbol)
           << std::setw(9) << symbol_or_dash(rank.leveraged_short_symbol)
           << '\n';
}

void print_rotation_group(
    std::ostream& output,
    std::string_view label,
    const std::vector<const domain::RankedEtf*>& ranks
)
{
    output << "\n" << label << " (" << ranks.size() << ")\n";
    if (ranks.empty()) {
        output << "  NONE\n";
        return;
    }

    print_rotation_table_header(output);
    for (const auto* rank : ranks) {
        print_rotation_rank(output, *rank);
    }
}

void print_rotation_section(
    std::ostream& output,
    std::string_view title,
    const std::vector<domain::RankedEtf>& rankings
)
{
    output << "\n" << title << " (60-minute relative strength vs SPY)\n";
    const auto groups = analysis::RotationGrouper{}.group(rankings);
    print_rotation_group(output, "STRONG", groups.strong);
    print_rotation_group(output, "NEUTRAL", groups.neutral);
    print_rotation_group(output, "WEAK", groups.weak);
}

void print_directional_candidate(
    std::ostream& output,
    std::string_view label,
    const std::optional<domain::TradeCandidate>& candidate,
    const std::vector<domain::RankedEtf>& rankings
)
{
    output << "\n" << label << ' ';
    if (!candidate.has_value()) {
        output << "directional candidate only (no order): NONE\n";
        return;
    }

    if (candidate->side == domain::TradeSide::short_side) {
        output << "bearish reference ETF only (not monitored, no order): "
               << candidate->trade_symbol << " from "
               << candidate->signal_symbol << " signal\n";
        return;
    }

    output << "long candidate only (no order): "
           << candidate->trade_symbol << " from "
           << candidate->signal_symbol << " signal\n";
    const auto rank = std::ranges::find(
        rankings,
        candidate->signal_symbol,
        &domain::RankedEtf::symbol
    );
    if (rank != rankings.end() && rank->entry_zone.has_value()) {
        output << "Signal ETF entry zone: " << entry_zone_text(rank->entry_zone)
               << " | current " << std::fixed << std::setprecision(2)
               << rank->entry_zone->current_price << " | "
               << domain::to_string(rank->entry_zone->state) << '\n';
    }
    if (rank != rankings.end() && rank->leveraged_entry_zone.has_value()) {
        output << "Leveraged ETF entry zone: "
               << entry_zone_text(rank->leveraged_entry_zone)
               << " | current " << std::fixed << std::setprecision(2)
               << rank->leveraged_entry_zone->current_price << " | "
               << domain::to_string(rank->leveraged_entry_zone->state) << '\n';
    }
}

void print_scan_header(
    std::ostream& output,
    const domain::MarketScan& scan,
    const time::TimeZoneFormatter& formatter
)
{
    output << "Scan time: " << formatter.format(scan.epoch_seconds)
           << " | aligned market bars: " << scan.aligned_market_bar_count << '\n';
    output << "Market regime: " << domain::to_string(scan.market_regime) << "\n\n";
}

void print_market_section(std::ostream& output, const domain::MarketScan& scan)
{
    output << "MARKET ETFs (VWAP + EMA20 trend signal)\n";
    output << std::left << std::setw(8) << "symbol"
           << std::right << std::setw(12) << "close"
           << std::setw(12) << "VWAP"
           << std::setw(12) << "EMA20"
           << std::setw(12) << "EMA20 %"
           << std::setw(10) << "signal" << '\n';
    print_market_snapshot(output, scan.spy);
    print_market_snapshot(output, scan.qqq);

    output << "\nVIX RISK REFERENCE (context only; does not block signals)\n";
    if (!scan.vix.has_value()) {
        output << "VIX data unavailable (optional CBOE index market data)\n";
        return;
    }

    output << std::left << std::setw(8) << "symbol"
           << std::right << std::setw(12) << "close"
           << std::setw(12) << "EMA20"
           << std::setw(14) << "60-min %"
           << std::setw(12) << "trend" << '\n';
    output << std::left << std::setw(8) << "VIX"
           << std::right << std::setw(12) << std::fixed << std::setprecision(2)
           << scan.vix->close
           << std::setw(12) << scan.vix->ema20
           << std::setw(14) << std::setprecision(3) << scan.vix->change_60_min_percent
           << std::setw(12) << domain::to_string(scan.vix->trend) << '\n';
}

} // namespace

ConsoleScanPrinter::ConsoleScanPrinter(std::string time_zone)
    : time_zone_{std::move(time_zone)}
{
}

std::string ConsoleScanPrinter::render(
    const domain::MarketScan& scan,
    DashboardTab tab
) const
{
    const time::TimeZoneFormatter formatter{time_zone_};
    std::ostringstream output;
    print_scan_header(output, scan, formatter);
    switch (tab) {
    case DashboardTab::market:
        print_market_section(output, scan);
        break;
    case DashboardTab::sectors:
        print_rotation_section(output, "SECTOR ROTATION", scan.sector_rankings);
        print_directional_candidate(
            output,
            "Sector",
            scan.sector_candidate,
            scan.sector_rankings
        );
        break;
    case DashboardTab::industries:
        print_rotation_section(output, "INDUSTRY ROTATION", scan.rankings);
        print_directional_candidate(output, "Industry", scan.candidate, scan.rankings);
        break;
    }
    return output.str();
}

std::string ConsoleScanPrinter::render_all(const domain::MarketScan& scan) const
{
    const time::TimeZoneFormatter formatter{time_zone_};
    std::ostringstream output;
    print_scan_header(output, scan, formatter);
    print_market_section(output, scan);
    print_rotation_section(output, "SECTOR ROTATION", scan.sector_rankings);
    print_directional_candidate(
        output,
        "Sector",
        scan.sector_candidate,
        scan.sector_rankings
    );
    print_rotation_section(output, "INDUSTRY ROTATION", scan.rankings);
    print_directional_candidate(output, "Industry", scan.candidate, scan.rankings);
    return output.str();
}

} // namespace daytrader::presentation
