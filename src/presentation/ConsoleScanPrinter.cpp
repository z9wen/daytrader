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

[[nodiscard]] std::string ratio_text(const std::optional<double>& value)
{
    if (!value.has_value()) {
        return "-";
    }
    std::ostringstream output;
    output << std::fixed << std::setprecision(2) << *value << 'x';
    return output.str();
}

[[nodiscard]] std::string number_text(
    const std::optional<double>& value,
    int precision = 2
)
{
    if (!value.has_value()) {
        return "-";
    }
    std::ostringstream output;
    output << std::fixed << std::setprecision(precision) << *value;
    return output.str();
}

[[nodiscard]] std::string rs_pair_text(
    const std::optional<double>& versus_spy,
    const std::optional<double>& versus_qqq
)
{
    return number_text(versus_spy, 1) + '/' + number_text(versus_qqq, 1);
}

[[nodiscard]] std::string_view compact_vwap_state(domain::VwapStructureState state)
{
    switch (state) {
    case domain::VwapStructureState::unavailable:
        return "-";
    case domain::VwapStructureState::below:
        return "BELOW";
    case domain::VwapStructureState::reclaimed:
        return "RCLM";
    case domain::VwapStructureState::above_flat:
        return "ABOVE";
    case domain::VwapStructureState::above_rising:
        return "RISE";
    case domain::VwapStructureState::lost:
        return "LOST";
    }
    return "?";
}

[[nodiscard]] std::string compact_action_text(
    const domain::LongOpportunity& opportunity
)
{
    std::string_view entry;
    switch (opportunity.entry) {
    case domain::LongEntryDecision::watch:
        entry = "WATCH";
        break;
    case domain::LongEntryDecision::wait_for_vwap:
        entry = "WAIT";
        break;
    case domain::LongEntryDecision::ready:
        entry = "READY";
        break;
    case domain::LongEntryDecision::avoid:
        entry = "AVOID";
        break;
    }

    std::string_view held;
    switch (opportunity.if_held) {
    case domain::HoldingGuidance::hold:
        held = "HOLD";
        break;
    case domain::HoldingGuidance::protect:
        held = "PROT";
        break;
    case domain::HoldingGuidance::trim:
        held = "TRIM";
        break;
    case domain::HoldingGuidance::exit:
        held = "EXIT";
        break;
    }
    return std::string{entry} + '/' + std::string{held};
}

[[nodiscard]] RotationLayout rotation_layout(std::size_t columns)
{
    // The full diagnostic view is useful on genuinely wide terminals. At the
    // common 150-170 column size, a focused execution view is easier to scan.
    if (columns >= 180) {
        return RotationLayout::regular;
    }
    if (columns >= 116) {
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
        output << std::left << std::setw(6) << "symbol"
               << ' ' << std::setw(6) << "RVOL"
               << ' ' << std::setw(7) << "VWAP st"
               << ' ' << std::setw(9) << "15 S/Q"
               << ' ' << std::setw(9) << "30 S/Q"
               << ' ' << std::setw(9) << "60 S/Q"
               << ' ' << std::setw(16) << "entry zone"
               << ' ' << std::setw(10) << "entry state"
               << ' ' << std::setw(6) << "long"
               << ' ' << std::setw(16) << "leveraged zone"
               << ' ' << std::setw(10) << "lev state"
               << ' ' << std::setw(9) << "phase"
               << ' ' << std::setw(5) << "score"
               << ' ' << std::setw(8) << "entry"
               << ' ' << std::setw(7) << "if held" << '\n';
        return;
    }

    if (layout == RotationLayout::compact) {
        output << std::left << std::setw(5) << "sym"
               << ' ' << std::setw(6) << "RVOL"
               << ' ' << std::setw(6) << "VWAP"
               << ' ' << std::setw(9) << "15 S/Q"
               << ' ' << std::setw(9) << "60 S/Q"
               << ' ' << std::setw(13) << "entry zone"
               << ' ' << std::setw(6) << "state"
               << ' ' << std::setw(5) << "long"
               << ' ' << std::setw(13) << "lev zone"
               << ' ' << std::setw(6) << "state"
               << ' ' << std::setw(8) << "phase"
               << ' ' << std::setw(5) << "score"
               << ' ' << std::setw(10) << "entry/held" << '\n';
        return;
    }

    output << std::left << std::setw(5) << "sym"
           << ' ' << std::setw(5) << "RVOL"
           << ' ' << std::setw(5) << "VWAP"
           << ' ' << std::setw(8) << "60 S/Q"
           << ' ' << std::setw(11) << "entry zone"
           << ' ' << std::setw(5) << "state"
           << ' ' << std::setw(7) << "phase"
           << ' ' << std::setw(4) << "scr" << '\n';
}

void print_responsive_rotation_rank(
    std::ostream& output,
    const ResponsiveRank& row,
    RotationLayout layout
)
{
    const auto& rank = *row.rank;
    if (layout == RotationLayout::regular) {
        output << std::left << std::setw(6) << fit_text(rank.symbol, 6)
               << ' ' << std::right << std::setw(6)
               << fit_text(ratio_text(rank.relative_volume.bar_ratio), 6)
               << ' ' << std::setw(7) << compact_vwap_state(rank.vwap_structure)
               << ' ' << std::setw(9) << rs_pair_text(
                      rank.relative_strength_vs_spy.fifteen_minute_percent,
                      rank.relative_strength_vs_qqq.fifteen_minute_percent
                  )
               << ' ' << std::setw(9) << rs_pair_text(
                      rank.relative_strength_vs_spy.thirty_minute_percent,
                      rank.relative_strength_vs_qqq.thirty_minute_percent
                  )
               << ' ' << std::setw(9) << rs_pair_text(
                      rank.relative_strength_vs_spy.sixty_minute_percent,
                      rank.relative_strength_vs_qqq.sixty_minute_percent
                  )
               << ' ' << std::setw(16) << compact_zone_text(rank.entry_zone, 16)
               << ' ' << std::setw(10)
               << fit_text(entry_zone_state(rank.entry_zone), 10)
               << ' ' << std::setw(6)
               << fit_text(symbol_or_dash(rank.leveraged_long_symbol), 6)
               << ' ' << std::setw(16)
               << compact_zone_text(rank.leveraged_entry_zone, 16)
               << ' ' << std::setw(10)
               << fit_text(entry_zone_state(rank.leveraged_entry_zone), 10)
               << ' ' << std::setw(9)
               << fit_text(domain::to_string(rank.long_opportunity.phase), 9)
               << ' ' << std::setw(5) << rank.long_opportunity.bullish_score
               << ' ' << std::setw(8)
               << fit_text(domain::to_string(rank.long_opportunity.entry), 8)
               << ' ' << std::setw(7)
               << fit_text(domain::to_string(rank.long_opportunity.if_held), 7)
               << '\n';
        return;
    }

    if (layout == RotationLayout::compact) {
        output << std::left << std::setw(5) << fit_text(rank.symbol, 5)
               << ' ' << std::right << std::setw(6)
               << fit_text(ratio_text(rank.relative_volume.bar_ratio), 6)
               << ' ' << std::setw(6) << compact_vwap_state(rank.vwap_structure)
               << ' ' << std::setw(9) << rs_pair_text(
                      rank.relative_strength_vs_spy.fifteen_minute_percent,
                      rank.relative_strength_vs_qqq.fifteen_minute_percent
                  )
               << ' ' << std::setw(9) << rs_pair_text(
                      rank.relative_strength_vs_spy.sixty_minute_percent,
                      rank.relative_strength_vs_qqq.sixty_minute_percent
                  )
               << ' ' << std::setw(13) << compact_zone_text(rank.entry_zone, 13)
               << ' ' << std::setw(6)
               << fit_text(compact_zone_state(rank.entry_zone), 6)
               << ' ' << std::setw(5)
               << fit_text(symbol_or_dash(rank.leveraged_long_symbol), 5)
               << ' ' << std::setw(13)
               << compact_zone_text(rank.leveraged_entry_zone, 13)
               << ' ' << std::setw(6)
               << fit_text(compact_zone_state(rank.leveraged_entry_zone), 6)
               << ' ' << std::setw(8)
               << fit_text(domain::to_string(rank.long_opportunity.phase), 8)
               << ' ' << std::setw(5) << rank.long_opportunity.bullish_score
               << ' ' << std::setw(10)
               << fit_text(compact_action_text(rank.long_opportunity), 10)
               << '\n';
        return;
    }

    output << std::left << std::setw(5) << fit_text(rank.symbol, 5)
           << ' ' << std::right << std::setw(5)
           << fit_text(ratio_text(rank.relative_volume.bar_ratio), 5)
           << ' ' << std::setw(5) << compact_vwap_state(rank.vwap_structure)
           << ' ' << std::setw(8) << fit_text(rs_pair_text(
                  rank.relative_strength_vs_spy.sixty_minute_percent,
                  rank.relative_strength_vs_qqq.sixty_minute_percent
              ), 8)
           << ' ' << std::setw(11) << compact_zone_text(rank.entry_zone, 11)
           << ' ' << std::setw(5)
           << fit_text(compact_zone_state(rank.entry_zone), 5)
           << ' ' << std::setw(7)
           << fit_text(domain::to_string(rank.long_opportunity.phase), 7)
           << ' ' << std::setw(4) << rank.long_opportunity.bullish_score << '\n';
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
    if (candidate->side == domain::CandidateSide::short_side) {
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
    output << std::left << std::setw(6) << snapshot.symbol
           << ' ' << std::right << std::setw(9) << std::fixed << std::setprecision(2)
           << snapshot.close;
    if (snapshot.session_vwap.has_value()) {
        output << ' ' << std::setw(9) << *snapshot.session_vwap;
    } else {
        output << ' ' << std::setw(9) << '-';
    }
    output << ' ' << std::setw(7) << domain::to_string(snapshot.vwap_structure)
           << ' ' << std::setw(6) << ratio_text(snapshot.relative_volume.bar_ratio)
           << ' ' << std::setw(6) << ratio_text(snapshot.relative_volume.cumulative_ratio)
           << ' ' << std::setw(8) << std::setprecision(4)
           << snapshot.ema20_change_percent
           << ' ' << std::setw(7) << domain::to_string(snapshot.trend_signal)
           << '\n';
}

void print_tqqq_execution(
    std::ostream& output,
    const domain::EtfSnapshot& snapshot,
    const std::optional<domain::EntryZone>& entry_zone
)
{
    output << std::left << std::setw(6) << fit_text(snapshot.symbol, 6)
           << ' ' << std::right << std::fixed << std::setprecision(2)
           << std::setw(9) << snapshot.close;
    if (snapshot.session_vwap.has_value()) {
        output << ' ' << std::setw(9) << *snapshot.session_vwap;
    } else {
        output << ' ' << std::setw(9) << '-';
    }
    output << ' ' << std::setw(7) << domain::to_string(snapshot.vwap_structure)
           << ' ' << std::setw(6) << ratio_text(snapshot.relative_volume.bar_ratio)
           << ' ' << std::setw(15) << compact_zone_text(entry_zone, 15)
           << ' ' << std::setw(10) << fit_text(entry_zone_state(entry_zone), 10)
           << ' ' << std::setw(7) << domain::to_string(snapshot.trend_signal)
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
           << std::setw(10) << "VWAP st"
           << std::setw(9) << "RVOL"
           << std::setw(9) << "S15 %"
           << std::setw(9) << "S30 %"
           << std::setw(9) << "S60 %"
           << std::setw(9) << "Q15 %"
           << std::setw(9) << "Q30 %"
           << std::setw(9) << "Q60 %"
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
           << std::setw(10) << domain::to_string(rank.vwap_structure)
           << std::setw(9) << ratio_text(rank.relative_volume.bar_ratio)
           << std::setw(9) << number_text(
                  rank.relative_strength_vs_spy.fifteen_minute_percent
              )
           << std::setw(9) << number_text(
                  rank.relative_strength_vs_spy.thirty_minute_percent
              )
           << std::setw(9) << number_text(
                  rank.relative_strength_vs_spy.sixty_minute_percent
              )
           << std::setw(9) << number_text(
                  rank.relative_strength_vs_qqq.fifteen_minute_percent
              )
           << std::setw(9) << number_text(
                  rank.relative_strength_vs_qqq.thirty_minute_percent
              )
           << std::setw(9) << number_text(
                  rank.relative_strength_vs_qqq.sixty_minute_percent
              )
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
    output << "\n" << title
           << " (RS 15/30/60 minutes; S=vs SPY, Q=vs QQQ)\n";
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

    if (candidate->side == domain::CandidateSide::short_side) {
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
    output << "MARKET DIRECTION (SPY/QQQ determine context)\n";
    output << std::left << std::setw(6) << "symbol"
           << ' ' << std::setw(9) << "close"
           << ' ' << std::setw(9) << "VWAP"
           << ' ' << std::setw(7) << "VWAP st"
           << ' ' << std::setw(6) << "RVOL"
           << ' ' << std::setw(6) << "cRVOL"
           << ' ' << std::setw(8) << "EMA20 %"
           << ' ' << std::setw(7) << "signal" << '\n';
    print_market_snapshot(output, scan.spy);
    print_market_snapshot(output, scan.qqq);

    output << "\nTQQQ EXECUTION (not used to determine market regime)\n";
    if (!scan.tqqq.has_value()) {
        output << "TQQQ data unavailable\n";
    } else {
        output << std::left << std::setw(6) << "symbol"
               << ' ' << std::setw(9) << "close"
               << ' ' << std::setw(9) << "VWAP"
               << ' ' << std::setw(7) << "VWAP st"
               << ' ' << std::setw(6) << "RVOL"
               << ' ' << std::setw(15) << "entry zone"
               << ' ' << std::setw(10) << "state"
               << ' ' << std::setw(7) << "signal" << '\n';
        print_tqqq_execution(output, *scan.tqqq, scan.tqqq_entry_zone);
    }

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

void print_trade_section(
    std::ostream& output,
    const domain::MarketScan& scan,
    const time::TimeZoneFormatter& formatter,
    std::size_t columns
)
{
    const auto& context = scan.live_context;
    const bool compact = columns < 90;
    const bool regular_session = context.updated_epoch_seconds > 0
        && formatter.minutes_since_midnight(context.updated_epoch_seconds) >= 9 * 60 + 30
        && formatter.minutes_since_midnight(context.updated_epoch_seconds) < 16 * 60;
    output << "LIVE TRADE CONTEXT (read-only; MFE starts when this process observes the lot)\n";
    if (context.updated_epoch_seconds > 0) {
        output << "Live update: " << formatter.format(context.updated_epoch_seconds) << '\n';
    } else {
        output << "Live context is connecting on the secondary IBKR client\n";
    }

    output << "\nDAY-TRADE POSITIONS (configured signal and long-leveraged ETFs only)\n";
    if (!context.positions_ready) {
        output << "Waiting for the initial IBKR position snapshot\n";
    } else if (context.positions.empty()) {
        output << "No open day-trade positions\n";
    } else {
        if (compact) {
            output << std::left << std::setw(6) << "symbol"
                   << ' ' << std::setw(8) << "qty"
                   << ' ' << std::setw(8) << "avg"
                   << ' ' << std::setw(8) << "mark"
                   << ' ' << std::setw(9) << "uPnL"
                   << ' ' << std::setw(9) << "peak MFE"
                   << ' ' << std::setw(9) << "giveback"
                   << ' ' << std::setw(6) << "gb%" << '\n';
        } else {
            output << std::left << std::setw(8) << "symbol"
                   << ' ' << std::setw(11) << "quantity"
                   << ' ' << std::setw(11) << "avg cost"
                   << ' ' << std::setw(11) << "mark"
                   << ' ' << std::setw(12) << "uPnL"
                   << ' ' << std::setw(12) << "peak MFE"
                   << ' ' << std::setw(12) << "giveback"
                   << ' ' << std::setw(11) << "giveback%" << '\n';
        }
        for (const auto& position : context.positions) {
            if (compact) {
                output << std::left << std::setw(6) << fit_text(position.symbol, 6)
                       << ' ' << std::right << std::fixed << std::setprecision(2)
                       << std::setw(8) << position.quantity
                       << ' ' << std::setw(8) << position.average_cost
                       << ' ' << std::setw(8) << number_text(position.market_price)
                       << ' ' << std::setw(9) << number_text(position.unrealized_pnl)
                       << ' ' << std::setw(9)
                       << number_text(position.peak_unrealized_pnl)
                       << ' ' << std::setw(9) << number_text(position.giveback_amount)
                       << ' ' << std::setw(6)
                       << number_text(position.giveback_percent, 1) << '\n';
            } else {
                output << std::left << std::setw(8) << fit_text(position.symbol, 8)
                       << ' ' << std::right << std::fixed << std::setprecision(2)
                       << std::setw(11) << position.quantity
                       << ' ' << std::setw(11) << position.average_cost
                       << ' ' << std::setw(11) << number_text(position.market_price)
                       << ' ' << std::setw(12) << number_text(position.unrealized_pnl)
                       << ' ' << std::setw(12)
                       << number_text(position.peak_unrealized_pnl)
                       << ' ' << std::setw(12) << number_text(position.giveback_amount)
                       << ' ' << std::setw(11)
                       << number_text(position.giveback_percent, 1) << '\n';
            }
        }
    }

    output << "\nLIVE ORDER FLOW (price-forming Last + BidAsk; rolling 30s/60s)\n";
    if (!context.order_flow_connected) {
        output << "Waiting for QQQ/SOXX tick-by-tick subscriptions\n";
        return;
    }
    if (!regular_session) {
        output << "RTH session closed; DeltaRatio resumes at 09:30 America/New_York\n";
    }
    if (compact) {
        output << std::left << std::setw(6) << "symbol"
               << ' ' << std::setw(7) << "Delta30"
               << ' ' << std::setw(7) << "Delta60"
               << ' ' << std::setw(7) << "accel"
               << ' ' << std::setw(13) << "pressure"
               << ' ' << std::setw(7) << "quality"
               << ' ' << std::setw(7) << "trades"
               << ' ' << std::setw(7) << "state" << '\n';
    } else {
        output << std::left << std::setw(8) << "symbol"
               << ' ' << std::setw(10) << "Delta30"
               << ' ' << std::setw(10) << "Delta60"
               << ' ' << std::setw(10) << "accel"
               << ' ' << std::setw(17) << "pressure"
               << ' ' << std::setw(10) << "quality"
               << ' ' << std::setw(9) << "trades"
               << ' ' << std::setw(8) << "state" << '\n';
    }
    for (const auto& flow : context.order_flow) {
        const auto acceleration = flow.assessment.has_value()
            ? flow.assessment->delta_acceleration_points
            : std::nullopt;
        const auto pressure = flow.assessment.has_value()
            ? domain::to_string(flow.assessment->pressure)
            : "NO_DATA";
        const auto quality = flow.assessment.has_value()
            ? std::optional<double>{flow.assessment->evidence_quality_percent}
            : std::nullopt;
        const auto state = !regular_session
            ? "CLOSED"
            : (flow.thirty_seconds.complete && flow.one_minute.complete
                ? "YES"
                : "WARM");
        if (compact) {
            output << std::left << std::setw(6) << fit_text(flow.symbol, 6)
                   << ' ' << std::right << std::setw(7)
                   << number_text(flow.thirty_seconds.flow.delta_ratio_percent, 1)
                   << ' ' << std::setw(7)
                   << number_text(flow.one_minute.flow.delta_ratio_percent, 1)
                   << ' ' << std::setw(7) << number_text(acceleration, 1)
                   << ' ' << std::setw(13) << fit_text(pressure, 13)
                   << ' ' << std::setw(7) << number_text(quality, 0)
                   << ' ' << std::setw(7) << flow.thirty_seconds.flow.trade_count
                   << ' ' << std::setw(7) << state << '\n';
        } else {
            output << std::left << std::setw(8) << fit_text(flow.symbol, 8)
                   << ' ' << std::right << std::setw(10)
                   << number_text(flow.thirty_seconds.flow.delta_ratio_percent, 1)
                   << ' ' << std::setw(10)
                   << number_text(flow.one_minute.flow.delta_ratio_percent, 1)
                   << ' ' << std::setw(10) << number_text(acceleration, 1)
                   << ' ' << std::setw(17) << pressure
                   << ' ' << std::setw(10) << number_text(quality, 0)
                   << ' ' << std::setw(9) << flow.thirty_seconds.flow.trade_count
                   << ' ' << std::setw(8) << state << '\n';
        }
    }
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
    case DashboardTab::trade:
        print_trade_section(output, scan, formatter, 200);
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
    if (tab == DashboardTab::trade) {
        std::ostringstream trade;
        print_trade_section(trade, scan, formatter, viewport.columns);
        std::istringstream lines{trade.str()};
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
        title + " | RS S/Q = vs SPY/QQQ",
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
    output << '\n';
    print_trade_section(output, scan, formatter, 200);
    return output.str();
}

} // namespace daytrader::presentation
