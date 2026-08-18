#include "daytrader/presentation/ConsoleScanPrinter.hpp"

#include "daytrader/analysis/RotationGrouper.hpp"
#include "daytrader/time/TimeZoneFormatter.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <optional>
#include <ostream>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace daytrader::presentation {
namespace {

enum class RotationLayout {
    minimal,
    compact,
    regular,
};

struct ResponsiveRank {
    std::string_view group;
    const domain::RankedEtf* rank{};
};

[[nodiscard]] std::string_view symbol_or_dash(const std::string& symbol);
[[nodiscard]] std::string_view entry_zone_state(
    const std::optional<domain::EntryZone>& zone
);

[[nodiscard]] std::string fit_text(std::string_view value, std::size_t width)
{
    if (value.size() <= width) {
        return std::string{value};
    }
    if (width == 0) {
        return {};
    }
    if (width == 1) {
        return "~";
    }
    return std::string{value.substr(0, width - 1)} + '~';
}

[[nodiscard]] std::string compact_zone_text(
    const std::optional<domain::EntryZone>& zone,
    std::size_t width
)
{
    if (!zone.has_value()) {
        return "-";
    }
    std::ostringstream output;
    output << std::fixed << std::setprecision(2)
           << zone->lower_price << '-' << zone->upper_price;
    return fit_text(output.str(), width);
}

[[nodiscard]] std::string_view compact_zone_state(
    const std::optional<domain::EntryZone>& zone
)
{
    if (!zone.has_value()) {
        return "-";
    }
    switch (zone->state) {
    case domain::EntryZoneState::in_zone:
        return "IN";
    case domain::EntryZoneState::extended:
        return "EXT";
    case domain::EntryZoneState::below_zone:
        return "BLW";
    case domain::EntryZoneState::trend_unconfirmed:
        return "NO_TR";
    }
    return "?";
}

[[nodiscard]] RotationLayout rotation_layout(std::size_t columns)
{
    if (columns >= 136) {
        return RotationLayout::regular;
    }
    if (columns >= 100) {
        return RotationLayout::compact;
    }
    return RotationLayout::minimal;
}

[[nodiscard]] std::vector<ResponsiveRank> responsive_ranks(
    const std::vector<domain::RankedEtf>& rankings
)
{
    const auto groups = analysis::RotationGrouper{}.group(rankings);
    std::vector<ResponsiveRank> rows;
    rows.reserve(rankings.size());
    const auto append = [&rows](
        std::string_view label,
        const std::vector<const domain::RankedEtf*>& ranks
    ) {
        for (const auto* rank : ranks) {
            rows.push_back(ResponsiveRank{.group = label, .rank = rank});
        }
    };
    append("STRONG", groups.strong);
    append("NEUTRAL", groups.neutral);
    append("WEAK", groups.weak);
    return rows;
}

void print_responsive_rotation_header(
    std::ostream& output,
    RotationLayout layout
)
{
    if (layout == RotationLayout::regular) {
        output << std::left << std::setw(7) << "symbol"
               << std::right << std::setw(9) << "close"
               << std::setw(9) << "VWAP"
               << std::setw(7) << "RS %"
               << std::setw(18) << "entry zone"
               << "  " << std::setw(11) << "entry state"
               << std::setw(7) << "long"
               << std::setw(18) << "leveraged zone"
               << "  " << std::setw(11) << "lev state"
               << std::setw(10) << "phase"
               << std::setw(6) << "score"
               << std::setw(10) << "entry"
               << std::setw(9) << "if held" << '\n';
        return;
    }

    if (layout == RotationLayout::compact) {
        output << std::left << std::setw(6) << "sym"
               << std::right << std::setw(7) << "RS %"
               << std::setw(15) << "entry zone"
               << "  " << std::setw(8) << "state"
               << std::setw(6) << "long"
               << std::setw(15) << "lev zone"
               << "  " << std::setw(8) << "state"
               << std::setw(9) << "phase"
               << std::setw(5) << "score"
               << std::setw(9) << "entry"
               << std::setw(8) << "held" << '\n';
        return;
    }

    output << std::left << std::setw(6) << "sym"
           << std::right << std::setw(7) << "RS %"
           << std::setw(14) << "entry zone"
           << "  " << std::setw(7) << "state"
           << std::setw(6) << "long"
           << std::setw(14) << "lev zone"
           << "  " << std::setw(7) << "state"
           << std::setw(8) << "phase"
           << std::setw(5) << "score" << '\n';
}

void print_responsive_rotation_rank(
    std::ostream& output,
    const ResponsiveRank& row,
    RotationLayout layout
)
{
    const auto& rank = *row.rank;
    if (layout == RotationLayout::regular) {
        output << std::left << std::setw(7) << fit_text(rank.symbol, 6)
               << std::right << std::setw(9) << std::fixed << std::setprecision(2)
               << rank.close;
        if (rank.session_vwap.has_value()) {
            output << std::setw(9) << *rank.session_vwap;
        } else {
            output << std::setw(9) << '-';
        }
        output << std::setw(7) << std::setprecision(2)
               << rank.relative_change_60_min_percent
               << std::setw(18) << compact_zone_text(rank.entry_zone, 17)
               << "  " << std::setw(11)
               << fit_text(entry_zone_state(rank.entry_zone), 10)
               << std::setw(7) << fit_text(symbol_or_dash(rank.leveraged_long_symbol), 6)
               << std::setw(18) << compact_zone_text(rank.leveraged_entry_zone, 17)
               << "  " << std::setw(11)
               << fit_text(entry_zone_state(rank.leveraged_entry_zone), 10)
               << std::setw(10)
               << fit_text(domain::to_string(rank.long_opportunity.phase), 9)
               << std::setw(6) << rank.long_opportunity.bullish_score
               << std::setw(10)
               << fit_text(domain::to_string(rank.long_opportunity.entry), 9)
               << std::setw(9)
               << fit_text(domain::to_string(rank.long_opportunity.if_held), 8)
               << '\n';
        return;
    }

    if (layout == RotationLayout::compact) {
        output << std::left << std::setw(6) << fit_text(rank.symbol, 5)
               << std::right << std::fixed << std::setprecision(2)
               << std::setw(7) << rank.relative_change_60_min_percent
               << std::setw(15) << compact_zone_text(rank.entry_zone, 14)
               << "  " << std::setw(8)
               << fit_text(compact_zone_state(rank.entry_zone), 7)
               << std::setw(6) << fit_text(symbol_or_dash(rank.leveraged_long_symbol), 5)
               << std::setw(15) << compact_zone_text(rank.leveraged_entry_zone, 14)
               << "  " << std::setw(8)
               << fit_text(compact_zone_state(rank.leveraged_entry_zone), 7)
               << std::setw(9)
               << fit_text(domain::to_string(rank.long_opportunity.phase), 8)
               << std::setw(5) << rank.long_opportunity.bullish_score
               << std::setw(9)
               << fit_text(domain::to_string(rank.long_opportunity.entry), 8)
               << std::setw(8)
               << fit_text(domain::to_string(rank.long_opportunity.if_held), 7)
               << '\n';
        return;
    }

    output << std::left << std::setw(6) << fit_text(rank.symbol, 5)
           << std::right << std::fixed << std::setprecision(2)
           << std::setw(7) << rank.relative_change_60_min_percent
           << std::setw(14) << compact_zone_text(rank.entry_zone, 13)
           << "  " << std::setw(7)
           << fit_text(compact_zone_state(rank.entry_zone), 6)
           << std::setw(6) << fit_text(symbol_or_dash(rank.leveraged_long_symbol), 5)
           << std::setw(14) << compact_zone_text(rank.leveraged_entry_zone, 13)
           << "  " << std::setw(7)
           << fit_text(compact_zone_state(rank.leveraged_entry_zone), 6)
           << std::setw(8)
           << fit_text(domain::to_string(rank.long_opportunity.phase), 7)
           << std::setw(5) << rank.long_opportunity.bullish_score << '\n';
}

[[nodiscard]] std::string rotation_header_line(RotationLayout layout)
{
    std::ostringstream output;
    print_responsive_rotation_header(output, layout);
    auto line = output.str();
    if (!line.empty() && line.back() == '\n') {
        line.pop_back();
    }
    return line;
}

[[nodiscard]] std::string rotation_rank_line(
    const ResponsiveRank& row,
    RotationLayout layout
)
{
    std::ostringstream output;
    print_responsive_rotation_rank(output, row, layout);
    auto line = output.str();
    if (!line.empty() && line.back() == '\n') {
        line.pop_back();
    }
    return line;
}

[[nodiscard]] std::vector<std::vector<std::string>> responsive_rotation_pages(
    const std::vector<ResponsiveRank>& rows,
    RotationLayout layout,
    std::size_t line_capacity
)
{
    line_capacity = std::max<std::size_t>(2, line_capacity);
    std::vector<std::vector<std::string>> pages(1);
    const auto start_page = [&pages] { pages.emplace_back(); };
    const auto append = [&pages, line_capacity, &start_page](std::string line) {
        if (pages.back().size() >= line_capacity) {
            start_page();
        }
        pages.back().push_back(std::move(line));
    };
    const auto header = rotation_header_line(layout);

    for (const std::string_view group : {"STRONG", "NEUTRAL", "WEAK"}) {
        std::vector<const ResponsiveRank*> group_rows;
        for (const auto& row : rows) {
            if (row.group == group) {
                group_rows.push_back(&row);
            }
        }

        const std::size_t group_start_lines = group_rows.empty() ? 2 : 3;
        const std::size_t separator_lines = pages.back().empty() ? 0 : 1;
        if (!pages.back().empty()
            && pages.back().size() + separator_lines + group_start_lines
                > line_capacity) {
            start_page();
        }
        if (!pages.back().empty()) {
            append("");
        }

        append(std::string{group} + " (" + std::to_string(group_rows.size()) + ")");
        if (group_rows.empty()) {
            append("  NONE");
            continue;
        }

        if (line_capacity >= 3) {
            append(header);
        }
        for (const auto* row : group_rows) {
            if (pages.back().size() >= line_capacity) {
                start_page();
                if (line_capacity >= 3) {
                    append(std::string{group} + " (continued)");
                    append(header);
                }
            }
            append(rotation_rank_line(*row, layout));
        }
    }
    if (!pages.empty() && pages.back().empty()) {
        pages.pop_back();
    }
    return pages;
}

[[nodiscard]] std::string candidate_summary(
    const std::optional<domain::TradeCandidate>& candidate
)
{
    if (!candidate.has_value()) {
        return "candidate: NONE";
    }
    if (candidate->side == domain::TradeSide::short_side) {
        return "short reference: " + candidate->trade_symbol
            + " from " + candidate->signal_symbol;
    }
    return "long candidate: " + candidate->trade_symbol
        + " from " + candidate->signal_symbol;
}

[[nodiscard]] std::string clipped_line(std::string text, std::size_t columns)
{
    return fit_text(text, std::max<std::size_t>(1, columns));
}

[[nodiscard]] std::string clip_lines(
    const std::string& text,
    std::size_t columns
)
{
    std::istringstream input{text};
    std::ostringstream output;
    std::string line;
    while (std::getline(input, line)) {
        output << clipped_line(std::move(line), columns) << '\n';
    }
    return output.str();
}

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

DashboardPage ConsoleScanPrinter::render_page(
    const domain::MarketScan& scan,
    DashboardTab tab,
    DashboardViewport viewport
) const
{
    viewport.columns = std::max<std::size_t>(40, viewport.columns);
    viewport.rows = std::max<std::size_t>(5, viewport.rows);

    const time::TimeZoneFormatter formatter{time_zone_};
    std::ostringstream output;
    output << clipped_line(
        "Scan " + formatter.format(scan.epoch_seconds)
            + " | bars " + std::to_string(scan.aligned_market_bar_count)
            + " | market " + std::string{domain::to_string(scan.market_regime)},
        viewport.columns
    ) << '\n';

    if (tab == DashboardTab::market) {
        std::ostringstream market;
        print_market_section(market, scan);
        std::istringstream lines{market.str()};
        std::string line;
        while (std::getline(lines, line)) {
            output << clipped_line(std::move(line), viewport.columns) << '\n';
        }
        return DashboardPage{.text = clip_lines(output.str(), viewport.columns)};
    }

    const auto& rankings = tab == DashboardTab::sectors
        ? scan.sector_rankings
        : scan.rankings;
    const auto& candidate = tab == DashboardTab::sectors
        ? scan.sector_candidate
        : scan.candidate;
    const auto title = tab == DashboardTab::sectors
        ? std::string{"SECTOR ROTATION"}
        : std::string{"INDUSTRY ROTATION"};
    const auto rows = responsive_ranks(rankings);
    constexpr std::size_t fixed_line_count = 3;
    const std::size_t body_line_capacity = std::max<std::size_t>(
        1,
        viewport.rows > fixed_line_count
            ? viewport.rows - fixed_line_count
            : 1
    );
    const auto layout = rotation_layout(viewport.columns);
    const auto pages = responsive_rotation_pages(rows, layout, body_line_capacity);
    const std::size_t page_count = std::max<std::size_t>(1, pages.size());
    const std::size_t page_index = std::min(
        viewport.requested_page,
        page_count - 1
    );

    output << clipped_line(
        title + " | grouped by relative strength",
        viewport.columns
    ) << '\n';
    if (!pages.empty()) {
        for (const auto& line : pages[page_index]) {
            output << line << '\n';
        }
    }
    output << clipped_line(
        "Page " + std::to_string(page_index + 1) + '/'
            + std::to_string(page_count) + " | " + candidate_summary(candidate)
            + (page_count > 1 ? " | [ / ] page" : ""),
        viewport.columns
    ) << '\n';

    return DashboardPage{
        .text = clip_lines(output.str(), viewport.columns),
        .page_index = page_index,
        .page_count = page_count,
    };
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
