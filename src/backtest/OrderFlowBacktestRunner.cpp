#include "daytrader/backtest/OrderFlowBacktestRunner.hpp"

#include "daytrader/analysis/OrderFlowWindowAnalyzer.hpp"
#include "daytrader/analysis/OrderFlowSignalAnalyzer.hpp"
#include "daytrader/analysis/TradeClassifier.hpp"
#include "daytrader/backtest/IbkrBacktestRunner.hpp"
#include "daytrader/ibkr/IbkrErrorClassifier.hpp"
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
#include <thread>

namespace daytrader::backtest {
namespace {

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

[[nodiscard]] bool covers_lookback(
    const domain::OrderFlowTicks& ticks,
    std::int64_t end_timestamp,
    int lookback_seconds
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
    const auto start = end_timestamp - lookback_seconds + 1;
    return earliest_trade <= start && earliest_quote <= start;
}

[[nodiscard]] domain::OrderFlowTicks fetch_ticks_with_reactive_retry(
    const config::AppConfig& config,
    const ibkr::HistoricalTickRequest& request
)
{
    while (true) {
        try {
            auto connection = config.ibkr;
            ibkr::TwsHistoricalTickClient client{std::move(connection)};
            return client.fetch(request);
        } catch (const std::exception& exception) {
            if (!ibkr::is_pacing_or_rate_limit_error(exception.what())) {
                throw;
            }
            std::cerr << "IBKR pacing response received; retrying the same tick "
                         "lookback after reconnect: "
                      << exception.what() << '\n';
            std::this_thread::sleep_for(config.monitoring.reconnect_delay);
        }
    }
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
        config.minute_data_directory.parent_path() / "order_flow_ticks"
    };
    const analysis::TradeClassifier classifier;
    const analysis::OrderFlowWindowAnalyzer analyzer;
    const analysis::OrderFlowSignalAnalyzer signal_analyzer;

    for (std::size_t candidate_index = 0;
         candidate_index < report.baseline.trade_log.size();
         ++candidate_index) {
        const auto& trade = report.baseline.trade_log[candidate_index];
        // Entry executes at the next one-minute bar open. The final included tick
        // is one second earlier, so the Order Flow gate cannot see after the fill.
        const std::int64_t evidence_end = trade.entry_timestamp - 1;
        OrderFlowCandidate candidate{.trade = trade};
        try {
            auto ticks = store.load("SOXX", evidence_end);
            if (ticks.has_value() && covers_lookback(*ticks, evidence_end, 300)) {
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
                ticks = fetch_ticks_with_reactive_retry(
                    config,
                    ibkr::HistoricalTickRequest{
                    .contract = signal_contract(config),
                    .end_timestamp = evidence_end,
                    .number_of_ticks = 1'000,
                    .minimum_lookback_seconds = 300,
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
                && candidate.assessment.combined_pressure_percent.has_value()
                && candidate.assessment.thirty_second_price_atr.has_value();
            if (!enough_data) {
                candidate.verdict = OrderFlowVerdict::insufficient_data;
            } else if (candidate.assessment.pressure
                           == domain::OrderFlowPressureState::buying_effective
                       || candidate.assessment.pressure
                           == domain::OrderFlowPressureState::selling_absorbed) {
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
