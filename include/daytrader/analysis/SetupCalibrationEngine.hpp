#pragma once

#include "daytrader/domain/InstrumentBars.hpp"
#include "daytrader/domain/MarketScan.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace daytrader::analysis {

struct SetupCalibrationSettings {
    std::chrono::minutes outcome_horizon{30};
    double favorable_target_atr{0.75};
    double adverse_stop_atr{0.40};
};

// Records each new BUILDING/READY wave, resolves its forward 1-minute outcome,
// persists the frozen feature vector, and attaches leakage-free empirical
// probabilities derived only from already resolved observations.
class SetupCalibrationEngine {
public:
    SetupCalibrationEngine(
        std::filesystem::path cache_path,
        std::string time_zone,
        SetupCalibrationSettings settings = {}
    );
    ~SetupCalibrationEngine();

    SetupCalibrationEngine(const SetupCalibrationEngine&) = delete;
    SetupCalibrationEngine& operator=(const SetupCalibrationEngine&) = delete;

    void observe_and_enrich(
        domain::MarketScan& scan,
        std::span<const domain::InstrumentBars> minute_history
    );
    void enrich(domain::MarketScan& scan) const;

    [[nodiscard]] std::size_t record_count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace daytrader::analysis
