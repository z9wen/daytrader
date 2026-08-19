#include "daytrader/analysis/SetupCalibrationEngine.hpp"

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

daytrader::domain::MarketScan building_scan(std::int64_t timestamp)
{
    using namespace daytrader;
    return domain::MarketScan{
        .epoch_seconds = timestamp,
        .spy = domain::EtfSnapshot{.symbol = "SPY"},
        .qqq = domain::EtfSnapshot{.symbol = "QQQ"},
        .rankings = {domain::RankedEtf{
            .symbol = "SOXX",
            .leveraged_long_symbol = "SOXL",
            .relative_strength_vs_spy = domain::RelativeStrengthHorizons{
                .fifteen_minute_percent = 0.2,
                .thirty_minute_percent = 0.3,
                .sixty_minute_percent = 0.5,
            },
            .relative_strength_vs_qqq = domain::RelativeStrengthHorizons{
                .fifteen_minute_percent = 0.1,
                .thirty_minute_percent = 0.2,
                .sixty_minute_percent = 0.4,
            },
            .relative_volume = domain::RelativeVolumeSnapshot{
                .bar_ratio = 1.4,
                .state = domain::RelativeVolumeState::expanding,
            },
            .leveraged_entry_zone = domain::EntryZone{
                .symbol = "SOXL",
                .lower_price = 99.5,
                .upper_price = 100.5,
                .current_price = 100.0,
                .session_vwap = 100.0,
                .atr14 = 2.0,
                .atr5 = 2.0,
                .state = domain::EntryZoneState::in_zone,
            },
            .long_opportunity = domain::LongOpportunity{
                .bullish_score = 60,
                .phase = domain::BullishPhase::building,
                .entry = domain::LongEntryDecision::watch,
            },
            .leveraged_execution = domain::LeveragedExecutionDecision{
                .entry = domain::LongEntryDecision::watch,
            },
        }},
    };
}

void records_resolves_and_calibrates_setup_waves()
{
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path()
        / ("daytrader-setup-calibration-" + std::to_string(unique));
    const auto path = directory / "setup_outcomes.csv";
    constexpr std::int64_t observed = 1'704'205'800; // 09:30 New York

    {
        daytrader::analysis::SetupCalibrationEngine engine{
            path,
            "America/New_York",
        };
        auto scan = building_scan(observed);
        std::vector<daytrader::domain::InstrumentBars> history{
            {.symbol = "SOXL", .bars = {{
                .epoch_seconds = observed,
                .open = 100.0,
                .high = 100.2,
                .low = 99.9,
                .close = 100.0,
            }}},
        };
        engine.observe_and_enrich(scan, history);
        require(engine.record_count() == 1, "a new BUILDING wave should be recorded once");
        require(!scan.rankings.front().long_opportunity.setup_probability.has_value(),
                "pending observations must not fabricate a probability");

        history.front().bars.push_back(daytrader::domain::MarketBar{
            .epoch_seconds = observed + 60,
            .open = 100.0,
            .high = 101.6,
            .low = 99.9,
            .close = 101.5,
        });
        scan.epoch_seconds = observed + 60;
        engine.observe_and_enrich(scan, history);
        const auto estimate = scan.rankings.front().long_opportunity.setup_probability;
        require(estimate.has_value(), "a resolved event should calibrate the live setup");
        require(estimate->samples == 1 && estimate->successes == 1,
                "calibration should disclose its exact sample depth");
        require(std::abs(estimate->success_probability_percent - 200.0 / 3.0) < 1e-9,
                "Beta smoothing should avoid presenting one sample as 100 percent");
    }

    daytrader::analysis::SetupCalibrationEngine reloaded{
        path,
        "America/New_York",
    };
    require(reloaded.record_count() == 1, "resolved setup outcomes should persist");
    auto same_wave = building_scan(observed + 60);
    const std::vector<daytrader::domain::InstrumentBars> resolved_history{
        {.symbol = "SOXL", .bars = {
            {
                .epoch_seconds = observed,
                .open = 100.0,
                .high = 100.2,
                .low = 99.9,
                .close = 100.0,
            },
            {
                .epoch_seconds = observed + 60,
                .open = 100.0,
                .high = 101.6,
                .low = 99.9,
                .close = 101.5,
            },
        }},
    };
    reloaded.observe_and_enrich(same_wave, resolved_history);
    require(reloaded.record_count() == 1,
            "a restart must not duplicate an uninterrupted setup wave");
    std::filesystem::remove_all(directory);
}

} // namespace

int main()
{
    try {
        records_resolves_and_calibrates_setup_waves();
        std::cout << "SetupCalibrationTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "SetupCalibrationTests failed: " << exception.what() << '\n';
        return 1;
    }
}
