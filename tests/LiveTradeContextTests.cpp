#include "daytrader/analysis/LiveTradeContextEnricher.hpp"
#include "daytrader/analysis/RelativeStrengthAnalyzer.hpp"
#include "daytrader/analysis/RelativeVolumeAnalyzer.hpp"
#include "daytrader/analysis/VwapStructureAnalyzer.hpp"
#include "daytrader/live/LiveOrderFlowTracker.hpp"
#include "daytrader/live/PositionTracker.hpp"
#include "daytrader/market_data/BarSeriesAligner.hpp"
#include "daytrader/time/TimeZoneFormatter.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using daytrader::domain::MarketBar;

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(double actual, double expected, double tolerance, const std::string& message)
{
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

void calculates_three_relative_strength_horizons()
{
    std::vector<MarketBar> signal;
    std::vector<MarketBar> benchmark;
    std::vector<daytrader::market_data::AlignedBarPair> pairs;
    for (std::size_t index = 0; index < 13; ++index) {
        const auto timestamp = 1'700'000'000
            + static_cast<std::int64_t>(index) * 300;
        signal.push_back(MarketBar{
            .epoch_seconds = timestamp,
            .close = 100.0 + static_cast<double>(index),
        });
        benchmark.push_back(MarketBar{
            .epoch_seconds = timestamp,
            .close = 100.0,
        });
    }
    for (std::size_t index = 0; index < signal.size(); ++index) {
        pairs.push_back(daytrader::market_data::AlignedBarPair{
            .epoch_seconds = signal[index].epoch_seconds,
            .signal = &signal[index],
            .benchmark = &benchmark[index],
        });
    }

    const auto result = daytrader::analysis::RelativeStrengthAnalyzer{}.analyze(pairs);
    require(result.fifteen_minute_percent.has_value(), "RS15 should be available");
    require(result.thirty_minute_percent.has_value(), "RS30 should be available");
    require(result.sixty_minute_percent.has_value(), "RS60 should be available");
    require_near(*result.fifteen_minute_percent, 112.0 / 109.0 * 100.0 - 100.0,
                 1e-9, "RS15 mismatch");
    require_near(*result.thirty_minute_percent, 112.0 / 106.0 * 100.0 - 100.0,
                 1e-9, "RS30 mismatch");
    require_near(*result.sixty_minute_percent, 12.0, 1e-9, "RS60 mismatch");
}

void detects_vwap_reclaim_and_loss()
{
    const daytrader::time::TimeZoneFormatter formatter{"America/New_York"};
    std::vector<MarketBar> bars{
        MarketBar{
            .epoch_seconds = 1'700'000'000,
            .close = 99.0,
            .volume = 100.0,
            .weighted_average_price = 100.0,
        },
        MarketBar{
            .epoch_seconds = 1'700'000'300,
            .close = 101.0,
            .volume = 100.0,
            .weighted_average_price = 100.0,
        },
    };
    std::vector<const MarketBar*> pointers{&bars[0], &bars[1]};
    const daytrader::analysis::VwapStructureAnalyzer analyzer;
    require(
        analyzer.analyze(pointers, formatter)
            == daytrader::domain::VwapStructureState::reclaimed,
        "crossing above VWAP should be RECLAIM"
    );

    bars[0].close = 101.0;
    bars[1].close = 99.0;
    require(
        analyzer.analyze(pointers, formatter)
            == daytrader::domain::VwapStructureState::lost,
        "crossing below VWAP should be LOST"
    );
}

void compares_volume_with_same_time_prior_sessions()
{
    const daytrader::time::TimeZoneFormatter formatter{"America/New_York"};
    constexpr std::int64_t first = 1'699'948'800;
    std::vector<MarketBar> bars;
    for (const double volume : {100.0, 120.0, 80.0, 150.0}) {
        bars.push_back(MarketBar{
            .epoch_seconds = first + static_cast<std::int64_t>(bars.size()) * 86'400,
            .volume = volume,
        });
    }
    std::vector<const MarketBar*> pointers;
    for (const auto& bar : bars) {
        pointers.push_back(&bar);
    }

    const auto result = daytrader::analysis::RelativeVolumeAnalyzer{}.analyze(
        pointers,
        formatter
    );
    require(result.bar_ratio.has_value(), "same-time RVOL should be available");
    require_near(*result.bar_ratio, 1.5, 1e-9, "RVOL should use the median baseline");
    require(result.baseline_sessions == 3, "RVOL should report baseline depth");
    require(
        result.state == daytrader::domain::RelativeVolumeState::expanding,
        "1.5x volume should be EXPAND"
    );
}

void tracks_position_mfe_and_giveback()
{
    daytrader::live::PositionTracker tracker;
    tracker.update_position("DU1", "SOXL", 123, 100.0, 50.0);
    tracker.update_market_price("DU1", 123, 55.0);
    auto positions = tracker.snapshot();
    require_near(*positions[0].unrealized_pnl, 500.0, 1e-9,
                 "Level-1 fallback unrealized P&L mismatch");
    tracker.update_pnl("DU1", 123, 400.0, 500.0, 6'000.0);
    tracker.update_pnl("DU1", 123, 250.0, 300.0, 5'800.0);
    positions = tracker.snapshot();
    require(positions.size() == 1, "one position should be tracked");
    require_near(*positions[0].market_price, 58.0, 1e-9, "mark price mismatch");
    require_near(*positions[0].peak_unrealized_pnl, 500.0, 1e-9, "MFE mismatch");
    require_near(*positions[0].giveback_amount, 200.0, 1e-9, "giveback mismatch");
    require_near(*positions[0].giveback_percent, 40.0, 1e-9, "giveback percent mismatch");

    tracker.update_position("DU1", "SOXL", 123, 50.0, 50.0);
    positions = tracker.snapshot();
    require(!positions[0].peak_unrealized_pnl.has_value(),
            "quantity changes should reset lot MFE");
}

void builds_complete_live_delta_windows()
{
    daytrader::live::LiveOrderFlowTracker tracker{"SOXX"};
    tracker.on_quote(930, 99.9, 100.0, 100.0, 100.0);
    tracker.on_trade(930, 100.0, 10.0);
    tracker.on_quote(970, 100.0, 100.1, 100.0, 100.0);
    tracker.on_trade(970, 100.1, 20.0);
    tracker.on_quote(995, 100.1, 100.2, 100.0, 100.0);
    tracker.on_trade(995, 100.2, 30.0);

    const auto result = tracker.snapshot(1'000);
    require(result.thirty_seconds.complete, "30-second live window should be warm");
    require(result.one_minute.complete, "60-second live window should be warm");
    require(result.thirty_seconds.flow.delta_ratio_percent.has_value(),
            "live Delta30 should be available");
    require_near(*result.thirty_seconds.flow.delta_ratio_percent, 100.0, 1e-9,
                 "aggressive ask trades should produce positive Delta");
}

void enriches_live_execution_from_signal_flow_and_position()
{
    using namespace daytrader;
    auto scan = domain::MarketScan{
        .qqq = domain::EtfSnapshot{
            .symbol = "QQQ",
            .trend_signal = domain::MarketTrendSignal::strong,
        },
        .tqqq = domain::EtfSnapshot{.symbol = "TQQQ"},
        .tqqq_entry_zone = domain::EntryZone{
            .symbol = "TQQQ",
            .state = domain::EntryZoneState::in_zone,
        },
        .rankings = {domain::RankedEtf{
            .symbol = "SOXX",
            .leveraged_long_symbol = "SOXL",
            .leveraged_entry_zone = domain::EntryZone{
                .symbol = "SOXL",
                .state = domain::EntryZoneState::in_zone,
            },
            .long_opportunity = domain::LongOpportunity{
                .bullish_score = 90,
                .phase = domain::BullishPhase::strong,
                .entry = domain::LongEntryDecision::wait_for_vwap,
                .if_held = domain::HoldingGuidance::hold,
            },
        }},
    };
    const auto buying = domain::OrderFlowAssessment{
        .evidence_quality_percent = 80.0,
        .pressure = domain::OrderFlowPressureState::buying_effective,
    };
    auto context = domain::LiveTradeContext{
        .positions = {domain::PositionSnapshot{
            .symbol = "SOXL",
            .quantity = 100.0,
            .average_cost = 50.0,
            .unrealized_pnl = 300.0,
            .peak_unrealized_pnl = 500.0,
            .giveback_amount = 200.0,
            .giveback_percent = 40.0,
        }},
        .order_flow = {
            domain::LiveOrderFlowSnapshot{.symbol = "QQQ", .assessment = buying},
            domain::LiveOrderFlowSnapshot{.symbol = "SOXX", .assessment = buying},
        },
    };

    const auto enriched = analysis::LiveTradeContextEnricher{}.enrich(
        std::move(scan),
        std::move(context)
    );
    require(enriched.tqqq_execution.has_value()
                && enriched.tqqq_execution->entry
                    == domain::LongEntryDecision::ready,
            "QQQ flow and TQQQ zone should be wired into live READY");
    require(enriched.rankings[0].leveraged_execution.entry
                == domain::LongEntryDecision::ready,
            "SOXX flow and SOXL zone should be wired into live READY");
    require(enriched.rankings[0].leveraged_execution.if_held
                == domain::HoldingGuidance::trim,
            "SOXL MFE giveback should be wired into live TRIM guidance");
}

} // namespace

int main()
{
    try {
        calculates_three_relative_strength_horizons();
        detects_vwap_reclaim_and_loss();
        compares_volume_with_same_time_prior_sessions();
        tracks_position_mfe_and_giveback();
        builds_complete_live_delta_windows();
        enriches_live_execution_from_signal_flow_and_position();
        std::cout << "LiveTradeContextTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "LiveTradeContextTests failed: " << exception.what() << '\n';
        return 1;
    }
}
