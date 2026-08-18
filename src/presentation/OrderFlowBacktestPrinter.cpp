#include "daytrader/presentation/OrderFlowBacktestPrinter.hpp"

#include "daytrader/time/TimeZoneFormatter.hpp"

#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace daytrader::presentation {
namespace {

void write_metric(
    std::ostream& output,
    const domain::OrderFlowWindow& window,
    int width
)
{
    if (!window.flow.delta_ratio_percent.has_value()) {
        output << std::setw(width) << "N/A";
        return;
    }
    std::ostringstream value;
    value << std::fixed << std::setprecision(1) << *window.flow.delta_ratio_percent;
    if (!window.complete) {
        value << '*';
    }
    output << std::setw(width) << value.str();
}

void write_optional(
    std::ostream& output,
    const std::optional<double>& value,
    int width,
    int precision
)
{
    if (value.has_value()) {
        output << std::setw(width) << std::fixed << std::setprecision(precision)
               << *value;
    } else {
        output << std::setw(width) << "N/A";
    }
}

} // namespace

OrderFlowBacktestPrinter::OrderFlowBacktestPrinter(std::string time_zone)
    : time_zone_{std::move(time_zone)}
{
}

std::string OrderFlowBacktestPrinter::render(
    const backtest::OrderFlowBacktestReport& report
) const
{
    std::ostringstream output;
    output << "\nSOXX ORDER-FLOW ENTRY FILTER\n"
           << "Baseline: " << report.baseline.strategy_name << " | "
           << report.baseline.first_session << " to " << report.baseline.last_session
           << " | " << report.baseline.trades << " candidate trades\n"
           << "CONFIRM requires positive 30s and 1m DeltaRatio with at least 80% "
              "classified volume. FLOW also evaluates ATR-normalized price response.\n"
           << "Tick evidence ends before entry; quality measures evidence, not direction.\n\n";

    output << std::left << std::setw(12) << "date"
           << std::setw(10) << "entry"
           << std::right << std::setw(8) << "net %"
           << std::setw(8) << "ATR %"
           << std::setw(8) << "ATRx"
           << std::setw(8) << "D30"
           << std::setw(8) << "D60"
           << std::setw(9) << "dDelta"
           << std::setw(10) << "P30 ATR"
           << std::setw(11) << "impactATR"
           << std::setw(9) << "score"
           << std::setw(9) << "quality"
           << std::setw(12) << "ATR state"
           << std::setw(17) << "flow"
           << std::setw(12) << "raw gate" << '\n';

    const time::TimeZoneFormatter formatter{time_zone_};
    for (const auto& candidate : report.candidates) {
        const auto formatted_entry = formatter.format(candidate.trade.entry_timestamp);
        const auto entry_time = formatted_entry.size() >= 19
            ? formatted_entry.substr(11, 8)
            : formatted_entry;
        output << std::left << std::setw(12) << candidate.trade.session_date
               << std::setw(10) << entry_time
               << std::right << std::fixed << std::setprecision(3)
               << std::setw(8) << candidate.trade.net_return_percent
               << std::setw(8) << std::setprecision(2)
               << candidate.trade.signal_atr_percent_at_entry
               << std::setw(8) << std::setprecision(2)
               << candidate.trade.signal_atr_expansion_ratio;
        write_metric(output, candidate.thirty_seconds, 8);
        write_metric(output, candidate.one_minute, 8);
        write_optional(
            output,
            candidate.assessment.delta_acceleration_points,
            9,
            1
        );
        write_optional(
            output,
            candidate.assessment.thirty_second_price_atr,
            10,
            3
        );
        write_optional(
            output,
            candidate.assessment.normalized_impact_atr,
            11,
            3
        );
        write_optional(
            output,
            candidate.assessment.directional_score,
            9,
            1
        );
        output << std::setw(9) << std::setprecision(1)
               << candidate.assessment.evidence_quality_percent
               << std::setw(12) << domain::to_string(candidate.assessment.volatility)
               << std::setw(17) << domain::to_string(candidate.assessment.pressure)
               << std::setw(12) << backtest::to_string(candidate.verdict) << '\n';
        if (!candidate.error.empty()) {
            output << "  data error: " << candidate.error << '\n';
        }
    }

    output << "\nOrder Flow kept " << report.confirmed << ", rejected "
           << report.rejected << ", insufficient " << report.insufficient << ".\n";
    output << "Tick cache: " << report.cache_hits << " reused, "
           << report.downloaded << " downloaded this run.\n";
    if (report.confirmed > 0) {
        output << "Confirmed subset: win rate " << std::fixed << std::setprecision(1)
               << report.confirmed_win_rate_percent << "% | average net "
               << std::setprecision(3) << report.confirmed_average_net_return_percent
               << "%\n";
    }
    const auto write_subset = [&output](
        const char* label,
        const backtest::OrderFlowSubsetStats& subset
    ) {
        output << label << ": " << subset.candidates << " candidates | win rate "
               << std::fixed << std::setprecision(1) << subset.win_rate_percent
               << "% | average net " << std::setprecision(3)
               << subset.average_net_return_percent << "%\n";
    };
    write_subset("Bullish flow", report.bullish_flow);
    write_subset("Bearish flow", report.bearish_flow);
    write_subset("Balanced flow", report.balanced_flow);
    output << "ATR is completed 5-minute SOXX ATR14; ATRx is ATR5 / ATR14. "
              "impactATR normalizes 30-second response to a hypothetical 100% DeltaRatio.\n"
           << "Score is a quality-adjusted -100..100 directional diagnostic, not a "
              "probability or an active strategy gate.\n"
           << "This selected-period pass is exploratory, not a stable edge.\n";
    return output.str();
}

} // namespace daytrader::presentation
