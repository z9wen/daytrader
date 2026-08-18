#include "daytrader/backtest/OrderFlowBacktestRunner.hpp"

#include "daytrader/analysis/OrderFlowWindowAnalyzer.hpp"
#include "daytrader/analysis/OrderFlowSignalAnalyzer.hpp"
#include "daytrader/analysis/TradeClassifier.hpp"
#include "daytrader/backtest/IbkrBacktestRunner.hpp"
#include "daytrader/ibkr/TwsHistoricalTickClient.hpp"
#include "daytrader/storage/OrderFlowTickCsvStore.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>

namespace daytrader::backtest {
namespace {

constexpr double minimum_classification_coverage = 80.0;

[[nodiscard]] const config::HistoricalDataSettings& signal_contract(
    const config::AppConfig& config
)
{
    const auto found = std::ranges::find(
        config.etfs,
        std::string{"SOXX"},
        [](const universe::EtfDefinition& etf) { return etf.market_data.symbol; }
    );
    if (found == config.etfs.end()) {
        throw std::runtime_error("ETF universe is missing SOXX");
    }
    return found->market_data;
}

[[nodiscard]] bool positive_and_covered(const domain::OrderFlowWindow& window)
{
    return window.complete && window.flow.delta_ratio_percent.has_value()
        && *window.flow.delta_ratio_percent > 0.0
        && window.flow.classification_coverage_percent
            >= minimum_classification_coverage;
}

[[nodiscard]] int sampled_seconds(
    const domain::OrderFlowTicks& ticks,
    std::int64_t end_timestamp
)
{
    std::int64_t earliest = end_timestamp;
    if (!ticks.trades.empty()) {
        earliest = std::min(earliest, ticks.trades.front().epoch_seconds);
    }
    if (!ticks.quotes.empty()) {
        earliest = std::min(earliest, ticks.quotes.front().epoch_seconds);
    }
    return static_cast<int>(std::max<std::int64_t>(0, end_timestamp - earliest));
}

[[nodiscard]] bool covers_one_minute(
    const domain::OrderFlowTicks& ticks,
    std::int64_t end_timestamp
)
{
    if (ticks.trades.empty() || ticks.quotes.empty()) {
        return false;
    }
    const auto earliest_trade = std::ranges::min(
        ticks.trades,
        {},
        &domain::TradeTick::epoch_seconds
    ).epoch_seconds;
    const auto earliest_quote = std::ranges::min(
        ticks.quotes,
        {},
        &domain::BidAskTick::epoch_seconds
    ).epoch_seconds;
    const auto start = end_timestamp - 59;
    return earliest_trade <= start && earliest_quote <= start;
}

void summarize(OrderFlowBacktestReport& report)
{
    double confirmed_return_sum{};
    double bullish_return_sum{};
    double bearish_return_sum{};
    double balanced_return_sum{};
    for (const auto& candidate : report.candidates) {
        switch (candidate.verdict) {
        case OrderFlowVerdict::confirmed:
            ++report.confirmed;
            confirmed_return_sum += candidate.trade.net_return_percent;
            if (candidate.trade.net_return_percent > 0.0) {
                ++report.confirmed_wins;
            }
            break;
        case OrderFlowVerdict::rejected:
            ++report.rejected;
            break;
        case OrderFlowVerdict::insufficient_data:
            ++report.insufficient;
            break;
        }

        auto* subset = &report.balanced_flow;
        auto* return_sum = &balanced_return_sum;
        switch (candidate.assessment.pressure) {
        case domain::OrderFlowPressureState::buying_effective:
        case domain::OrderFlowPressureState::selling_absorbed:
            subset = &report.bullish_flow;
            return_sum = &bullish_return_sum;
            break;
        case domain::OrderFlowPressureState::buying_absorbed:
        case domain::OrderFlowPressureState::selling_effective:
            subset = &report.bearish_flow;
            return_sum = &bearish_return_sum;
            break;
        case domain::OrderFlowPressureState::balanced:
            break;
        case domain::OrderFlowPressureState::insufficient_data:
            continue;
        }
        ++subset->candidates;
        *return_sum += candidate.trade.net_return_percent;
        if (candidate.trade.net_return_percent > 0.0) {
            ++subset->wins;
        }
    }
    if (report.confirmed > 0) {
        report.confirmed_win_rate_percent = static_cast<double>(report.confirmed_wins)
            / static_cast<double>(report.confirmed) * 100.0;
        report.confirmed_average_net_return_percent = confirmed_return_sum
            / static_cast<double>(report.confirmed);
    }

    const auto finish_subset = [](OrderFlowSubsetStats& subset, double return_sum) {
        if (subset.candidates == 0) {
            return;
        }
        subset.win_rate_percent = static_cast<double>(subset.wins)
            / static_cast<double>(subset.candidates) * 100.0;
        subset.average_net_return_percent = return_sum
            / static_cast<double>(subset.candidates);
    };
    finish_subset(report.bullish_flow, bullish_return_sum);
    finish_subset(report.bearish_flow, bearish_return_sum);
    finish_subset(report.balanced_flow, balanced_return_sum);
}

} // namespace

OrderFlowBacktestReport OrderFlowBacktestRunner::run(
    const config::AppConfig& config,
    int calendar_days
) const
{
    const auto baselines = IbkrBacktestRunner{}.run(config, calendar_days);
    if (baselines.empty()) {
        throw std::runtime_error("baseline backtest produced no reports");
    }

    OrderFlowBacktestReport report{.baseline = baselines.front()};
    report.candidates.reserve(report.baseline.trade_log.size());
    const storage::OrderFlowTickCsvStore store{
        config.data_directory.parent_path() / "order_flow_ticks"
    };
    const analysis::TradeClassifier classifier;
    const analysis::OrderFlowWindowAnalyzer analyzer;
    const analysis::OrderFlowSignalAnalyzer signal_analyzer;

    for (std::size_t candidate_index = 0;
         candidate_index < report.baseline.trade_log.size();
         ++candidate_index) {
        const auto& trade = report.baseline.trade_log[candidate_index];
        // Entry executes at the next 5-minute bar open. The final included tick
        // is one second earlier, so the Order Flow gate cannot see after the fill.
        const std::int64_t evidence_end = trade.entry_timestamp - 1;
        OrderFlowCandidate candidate{.trade = trade};
        try {
            auto ticks = store.load("SOXX", evidence_end);
            if (ticks.has_value() && covers_one_minute(*ticks, evidence_end)) {
                ++report.cache_hits;
                std::clog << '[' << candidate_index + 1 << '/'
                          << report.baseline.trade_log.size()
                          << "] Using cached SOXX Order Flow before "
                          << trade.session_date << '\n';
            } else {
                std::clog << '[' << candidate_index + 1 << '/'
                          << report.baseline.trade_log.size() << "] "
                          << (ticks.has_value() ? "Extending" : "Fetching")
                          << " SOXX Order Flow before " << trade.session_date
                          << " entry\n";
                auto connection = config.ibkr;
                connection.request_timeout = std::max(
                    connection.request_timeout,
                    std::chrono::seconds{45}
                );
                ibkr::TwsHistoricalTickClient client{std::move(connection)};
                ticks = client.fetch(ibkr::HistoricalTickRequest{
                    .contract = signal_contract(config),
                    .end_timestamp = evidence_end,
                    .number_of_ticks = 1'000,
                    .minimum_lookback_seconds = 60,
                    // Very active quote streams can exceed 6,000 updates per
                    // minute. Paging still stops as soon as both streams cover
                    // the requested minute.
                    .maximum_pages_per_stream = 12,
                });
                store.save(*ticks);
                ++report.downloaded;
            }

            candidate.raw_trades = ticks->trades.size();
            candidate.raw_quotes = ticks->quotes.size();
            candidate.sampled_seconds = sampled_seconds(*ticks, evidence_end);
            const auto classified = classifier.classify(ticks->trades, ticks->quotes);
            candidate.thirty_seconds = analyzer.analyze(
                classified,
                ticks->quotes,
                evidence_end - 29,
                evidence_end
            );
            candidate.one_minute = analyzer.analyze(
                classified,
                ticks->quotes,
                evidence_end - 59,
                evidence_end
            );
            candidate.five_minutes = analyzer.analyze(
                classified,
                ticks->quotes,
                evidence_end - 299,
                evidence_end
            );
            candidate.assessment = signal_analyzer.analyze(
                candidate.thirty_seconds,
                candidate.one_minute,
                trade.signal_atr_at_entry,
                trade.signal_atr_expansion_ratio
            );

            const bool enough_data = candidate.thirty_seconds.complete
                && candidate.one_minute.complete
                && candidate.thirty_seconds.flow.delta_ratio_percent.has_value()
                && candidate.one_minute.flow.delta_ratio_percent.has_value();
            if (!enough_data) {
                candidate.verdict = OrderFlowVerdict::insufficient_data;
            } else if (positive_and_covered(candidate.thirty_seconds)
                       && positive_and_covered(candidate.one_minute)) {
                candidate.verdict = OrderFlowVerdict::confirmed;
            } else {
                candidate.verdict = OrderFlowVerdict::rejected;
            }
        } catch (const std::exception& exception) {
            candidate.error = exception.what();
            candidate.verdict = OrderFlowVerdict::insufficient_data;
        }
        report.candidates.push_back(std::move(candidate));
    }
    summarize(report);
    return report;
}

} // namespace daytrader::backtest
