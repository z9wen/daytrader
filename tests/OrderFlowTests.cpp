#include "daytrader/analysis/OrderFlowAggregator.hpp"
#include "daytrader/analysis/OrderFlowSignalAnalyzer.hpp"
#include "daytrader/analysis/TradeClassifier.hpp"
#include "daytrader/analysis/OrderFlowWindowAnalyzer.hpp"
#include "daytrader/storage/OrderFlowTickCsvStore.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void classifies_quote_and_tick_rule_trades()
{
    const std::vector<daytrader::domain::BidAskTick> quotes{
        {.epoch_seconds = 99, .bid_price = 99.0, .ask_price = 101.0,
         .bid_size = 60.0, .ask_size = 40.0},
    };
    const std::vector<daytrader::domain::TradeTick> trades{
        {.epoch_seconds = 100, .sequence = 0, .price = 101.0, .size = 10.0},
        {.epoch_seconds = 101, .sequence = 1, .price = 100.0, .size = 20.0},
        {.epoch_seconds = 102, .sequence = 2, .price = 99.0, .size = 30.0},
        // No fresh quote here, so the uptick must still classify as a buy.
        {.epoch_seconds = 105, .sequence = 3, .price = 100.0, .size = 40.0},
    };

    const auto classified = daytrader::analysis::TradeClassifier{}.classify(
        trades,
        quotes
    );
    require(classified.size() == 4, "all valid trades should be retained");
    require(classified[0].side == daytrader::domain::TradeSide::buy,
            "a trade at ask should be a buy");
    require(classified[1].side == daytrader::domain::TradeSide::sell,
            "an inside-spread downtick should be a sell");
    require(classified[2].side == daytrader::domain::TradeSide::sell,
            "a trade at bid should be a sell");
    require(classified[3].side == daytrader::domain::TradeSide::buy,
            "the tick-rule fallback should classify an uptick");
}

void aggregates_delta_coverage_and_quote_imbalance()
{
    const std::vector<daytrader::domain::ClassifiedTrade> trades{
        {.trade = {.epoch_seconds = 120, .price = 10.0, .size = 70.0},
         .side = daytrader::domain::TradeSide::buy,
         .method = daytrader::domain::TradeClassificationMethod::quote_test},
        {.trade = {.epoch_seconds = 121, .price = 9.9, .size = 30.0},
         .side = daytrader::domain::TradeSide::sell,
         .method = daytrader::domain::TradeClassificationMethod::tick_rule},
        {.trade = {.epoch_seconds = 122, .price = 9.95, .size = 25.0},
         .side = daytrader::domain::TradeSide::unknown},
    };
    const std::vector<daytrader::domain::BidAskTick> quotes{
        {.epoch_seconds = 120, .bid_size = 75.0, .ask_size = 25.0},
        {.epoch_seconds = 121, .bid_size = 50.0, .ask_size = 50.0},
    };

    const auto bars = daytrader::analysis::OrderFlowAggregator{
        std::chrono::seconds{60}
    }.aggregate(trades, quotes);
    require(bars.size() == 1, "ticks should share one 60-second bucket");
    const auto& bar = bars.front();
    require(std::abs(bar.delta - 40.0) < 1e-9, "delta should be buy minus sell volume");
    require(bar.delta_ratio_percent.has_value()
                && std::abs(*bar.delta_ratio_percent - 40.0) < 1e-9,
            "delta ratio should use classified volume only");
    require(std::abs(bar.classification_coverage_percent - 80.0) < 1e-9,
            "coverage should expose unknown volume");
    require(std::abs(bar.quote_test_coverage_percent - 56.0) < 1e-9,
            "quote-test coverage should distinguish direct quote matches");
    require(bar.average_quote_imbalance_percent.has_value()
                && std::abs(*bar.average_quote_imbalance_percent - 25.0) < 1e-9,
            "quote imbalance should average valid quote observations");
    require(bar.price_change_basis_points.has_value()
                && std::abs(*bar.price_change_basis_points + 50.0) < 1e-9,
            "price response should use the first and last classified trade");
    require(bar.impact_efficiency.has_value()
                && std::abs(*bar.impact_efficiency + 1.25) < 1e-9,
            "impact efficiency should normalize price response by delta ratio");
    require(bar.first_trade_price == 10.0 && bar.last_trade_price == 9.95,
            "the price endpoints should remain available for ATR normalization");
}

void combines_delta_acceleration_with_atr_response()
{
    daytrader::domain::OrderFlowWindow thirty{
        .complete = true,
        .flow = {
            .delta_ratio_percent = 25.0,
            .classification_coverage_percent = 100.0,
            .quote_test_coverage_percent = 60.0,
            .first_trade_price = 100.0,
            .last_trade_price = 100.2,
            .trade_count = 200,
        },
    };
    daytrader::domain::OrderFlowWindow sixty{
        .complete = true,
        .flow = {
            .delta_ratio_percent = -10.0,
            .classification_coverage_percent = 100.0,
            .quote_test_coverage_percent = 70.0,
            .first_trade_price = 99.9,
            .last_trade_price = 100.2,
            .trade_count = 300,
        },
    };

    const auto assessment = daytrader::analysis::OrderFlowSignalAnalyzer{}.analyze(
        thirty,
        sixty,
        2.0,
        1.30
    );
    require(assessment.delta_acceleration_points.has_value()
                && std::abs(*assessment.delta_acceleration_points - 35.0) < 1e-9,
            "Delta acceleration should compare 30-second and one-minute pressure");
    require(assessment.thirty_second_price_atr.has_value()
                && std::abs(*assessment.thirty_second_price_atr - 0.1) < 1e-9,
            "price response should be measured in signal ATR units");
    require(assessment.normalized_impact_atr.has_value()
                && std::abs(*assessment.normalized_impact_atr - 0.4) < 1e-9,
            "impact should normalize ATR response by the Delta fraction");
    require(assessment.pressure
                == daytrader::domain::OrderFlowPressureState::buying_effective,
            "positive Delta with positive price response should be effective buying");
    require(assessment.directional_score.has_value()
                && std::abs(*assessment.directional_score - 18.208125) < 1e-6,
            "directional score should combine flow, response, and evidence quality");
    require(assessment.volatility
                == daytrader::domain::AtrVolatilityState::expanding,
            "ATR5 above ATR14 should identify an expanding volatility regime");

    thirty.flow.last_trade_price = 99.9;
    const auto absorbed = daytrader::analysis::OrderFlowSignalAnalyzer{}.analyze(
        thirty,
        sixty,
        2.0,
        1.30
    );
    require(absorbed.pressure
                == daytrader::domain::OrderFlowPressureState::buying_absorbed,
            "positive Delta with negative response should expose buyer absorption");

    thirty.flow.delta_ratio_percent = 2.0;
    const auto balanced = daytrader::analysis::OrderFlowSignalAnalyzer{}.analyze(
        thirty,
        sixty,
        2.0,
        1.30
    );
    require(balanced.pressure == daytrader::domain::OrderFlowPressureState::balanced,
            "small DeltaRatio should be treated as balanced flow");
    require(!balanced.normalized_impact_atr.has_value(),
            "near-zero Delta must not create an unstable impact ratio");
}

void marks_bounded_windows_incomplete()
{
    const std::vector<daytrader::domain::ClassifiedTrade> trades{
        {.trade = {.epoch_seconds = 190, .price = 10.0, .size = 60.0},
         .side = daytrader::domain::TradeSide::buy,
         .method = daytrader::domain::TradeClassificationMethod::quote_test},
        {.trade = {.epoch_seconds = 195, .price = 9.9, .size = 40.0},
         .side = daytrader::domain::TradeSide::sell,
         .method = daytrader::domain::TradeClassificationMethod::quote_test},
    };
    const std::vector<daytrader::domain::BidAskTick> quotes{
        {.epoch_seconds = 189, .bid_size = 50.0, .ask_size = 50.0},
    };
    const daytrader::analysis::OrderFlowWindowAnalyzer analyzer;
    const auto complete = analyzer.analyze(trades, quotes, 190, 200);
    const auto incomplete = analyzer.analyze(trades, quotes, 180, 200);

    require(complete.complete, "both event streams should reach the short window start");
    require(!incomplete.complete, "a bounded sample must not claim earlier coverage");
    require(complete.flow.delta_ratio_percent.has_value()
                && std::abs(*complete.flow.delta_ratio_percent - 20.0) < 1e-9,
            "window delta ratio should use only in-range trades");
}

void raw_tick_cache_round_trips()
{
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path()
        / ("daytrader-order-flow-store-" + std::to_string(unique));
    const daytrader::storage::OrderFlowTickCsvStore store{directory};
    const daytrader::domain::OrderFlowTicks original{
        .symbol = "SOXX",
        .requested_end_timestamp = 200,
        .trades = {{.epoch_seconds = 199, .sequence = 3, .price = 10.25, .size = 4.5}},
        .quotes = {{.epoch_seconds = 198, .sequence = 2, .bid_price = 10.2,
                    .ask_price = 10.3, .bid_size = 50.0, .ask_size = 40.0}},
    };

    store.save(original);
    const auto loaded = store.load("SOXX", 200);
    require(loaded.has_value(), "saved raw tick cache should be loadable");
    require(loaded->trades.size() == 1 && loaded->quotes.size() == 1,
            "both raw event streams should round-trip");
    require(std::abs(loaded->trades.front().size - 4.5) < 1e-12,
            "fractional trade sizes should retain precision");
    require(!store.load("SOXX", 201).has_value(),
            "a different candidate timestamp must not reuse the wrong cache");

    std::filesystem::remove_all(directory);
}

} // namespace

int main()
{
    try {
        classifies_quote_and_tick_rule_trades();
        aggregates_delta_coverage_and_quote_imbalance();
        marks_bounded_windows_incomplete();
        combines_delta_acceleration_with_atr_response();
        raw_tick_cache_round_trips();
        std::cout << "OrderFlowTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "OrderFlowTests failed: " << exception.what() << '\n';
        return 1;
    }
}
