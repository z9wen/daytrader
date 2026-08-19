#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace daytrader::domain {

enum class SetupKind {
    building,
    ready,
};

enum class SetupOutcome {
    pending,
    success,
    failure,
    ambiguous,
};

enum class CalibrationScope {
    score_bucket,
    symbol,
    session,
    global,
};

// Empirical 30-minute outcome probability. The sample count is always shown so
// a small posterior estimate cannot be mistaken for a mature success rate.
struct SetupProbabilityEstimate {
    double success_probability_percent{};
    double lower_confidence_percent{};
    double upper_confidence_percent{};
    std::size_t samples{};
    std::size_t successes{};
    CalibrationScope scope{CalibrationScope::global};
};

// Durable event row captured when BUILDING or READY starts. Features are frozen
// at observation time; future one-minute bars only fill outcome/MFE/MAE fields.
struct SetupOutcomeRecord {
    std::int64_t observed_epoch_seconds{};
    std::int64_t resolved_epoch_seconds{};
    std::string signal_symbol;
    std::string trade_symbol;
    SetupKind kind{SetupKind::building};
    bool regular_session{};
    int bullish_score{};
    double entry_price{};
    double atr{};
    double target_price{};
    double stop_price{};
    std::optional<double> relative_volume;
    std::optional<double> rs15_spy;
    std::optional<double> rs30_spy;
    std::optional<double> rs60_spy;
    std::optional<double> rs15_qqq;
    std::optional<double> rs30_qqq;
    std::optional<double> rs60_qqq;
    std::optional<double> delta30;
    std::optional<double> ofi30;
    std::optional<double> combined_pressure;
    std::optional<double> spread_basis_points;
    SetupOutcome outcome{SetupOutcome::pending};
    double maximum_favorable_excursion_atr{};
    double maximum_adverse_excursion_atr{};
    std::optional<std::int64_t> lead_seconds;
};

[[nodiscard]] constexpr std::string_view to_string(SetupKind kind)
{
    return kind == SetupKind::building ? "BUILDING" : "READY";
}

[[nodiscard]] constexpr std::string_view to_string(SetupOutcome outcome)
{
    switch (outcome) {
    case SetupOutcome::pending:
        return "PENDING";
    case SetupOutcome::success:
        return "SUCCESS";
    case SetupOutcome::failure:
        return "FAILURE";
    case SetupOutcome::ambiguous:
        return "AMBIGUOUS";
    }
    return "PENDING";
}

[[nodiscard]] constexpr std::string_view to_string(CalibrationScope scope)
{
    switch (scope) {
    case CalibrationScope::score_bucket:
        return "BUCKET";
    case CalibrationScope::symbol:
        return "SYMBOL";
    case CalibrationScope::session:
        return "SESSION";
    case CalibrationScope::global:
        return "GLOBAL";
    }
    return "GLOBAL";
}

} // namespace daytrader::domain
