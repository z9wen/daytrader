#include "daytrader/analysis/OrderFlowSignalAnalyzer.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace daytrader::analysis {
namespace {

[[nodiscard]] std::optional<double> price_response_atr(
    const domain::OrderFlowWindow& window,
    double signal_atr
)
{
    if (window.flow.first_trade_price.has_value()
        && window.flow.last_trade_price.has_value()) {
        return (*window.flow.last_trade_price - *window.flow.first_trade_price)
            / signal_atr;
    }
    if (window.flow.first_midpoint_price.has_value()
        && window.flow.last_midpoint_price.has_value()) {
        return (*window.flow.last_midpoint_price - *window.flow.first_midpoint_price)
            / signal_atr;
    }
    return std::nullopt;
}

} // namespace

OrderFlowSignalAnalyzer::OrderFlowSignalAnalyzer(OrderFlowSignalSettings settings)
    : settings_{settings}
{
    if (settings_.balanced_delta_ratio_percent < 0.0
        || settings_.balanced_delta_ratio_percent >= 100.0) {
        throw std::invalid_argument("balanced DeltaRatio threshold must be in [0, 100)");
    }
    if (!std::isfinite(settings_.microprice_full_scale_basis_points)
        || settings_.microprice_full_scale_basis_points <= 0.0) {
        throw std::invalid_argument("microprice full scale must be positive");
    }
    if (settings_.full_depth_trade_count == 0) {
        throw std::invalid_argument("Order Flow depth target must be positive");
    }
    if (!std::isfinite(settings_.price_response_deadband_atr)
        || settings_.price_response_deadband_atr < 0.0
        || !std::isfinite(settings_.price_response_full_scale_atr)
        || settings_.price_response_full_scale_atr
            <= settings_.price_response_deadband_atr) {
        throw std::invalid_argument("Order Flow ATR response thresholds are invalid");
    }
    if (!std::isfinite(settings_.compressed_atr_ratio)
        || !std::isfinite(settings_.expanding_atr_ratio)
        || !std::isfinite(settings_.extreme_atr_ratio)
        || settings_.compressed_atr_ratio <= 0.0
        || settings_.compressed_atr_ratio >= settings_.expanding_atr_ratio
        || settings_.expanding_atr_ratio >= settings_.extreme_atr_ratio) {
        throw std::invalid_argument("Order Flow ATR regime thresholds are invalid");
    }
}

domain::OrderFlowAssessment OrderFlowSignalAnalyzer::analyze(
    const domain::OrderFlowWindow& thirty_seconds,
    const domain::OrderFlowWindow& one_minute,
    double signal_atr,
    double signal_atr_expansion_ratio
) const
{
    if (!std::isfinite(signal_atr) || signal_atr <= 0.0) {
        throw std::invalid_argument("signal ATR must be finite and positive");
    }
    if (!std::isfinite(signal_atr_expansion_ratio)
        || signal_atr_expansion_ratio <= 0.0) {
        throw std::invalid_argument(
            "signal ATR expansion ratio must be finite and positive"
        );
    }

    domain::OrderFlowAssessment result;
    if (signal_atr_expansion_ratio < settings_.compressed_atr_ratio) {
        result.volatility = domain::AtrVolatilityState::compressed;
    } else if (signal_atr_expansion_ratio < settings_.expanding_atr_ratio) {
        result.volatility = domain::AtrVolatilityState::normal;
    } else if (signal_atr_expansion_ratio < settings_.extreme_atr_ratio) {
        result.volatility = domain::AtrVolatilityState::expanding;
    } else {
        result.volatility = domain::AtrVolatilityState::extreme;
    }
    const double average_classification_coverage =
        (thirty_seconds.flow.classification_coverage_percent
         + one_minute.flow.classification_coverage_percent) / 2.0;
    const double average_quote_coverage =
        (thirty_seconds.flow.quote_test_coverage_percent
         + one_minute.flow.quote_test_coverage_percent) / 2.0;
    const double completeness = thirty_seconds.complete && one_minute.complete
        ? 100.0
        : 0.0;
    const auto minimum_trade_count = std::min(
        thirty_seconds.flow.trade_count,
        one_minute.flow.trade_count
    );
    const double depth = std::min(
        100.0,
        static_cast<double>(minimum_trade_count)
            / static_cast<double>(settings_.full_depth_trade_count) * 100.0
    );
    result.evidence_quality_percent = std::clamp(
        0.25 * average_classification_coverage
            + 0.35 * average_quote_coverage
            + 0.25 * completeness
            + 0.15 * depth,
        0.0,
        100.0
    );

    if (thirty_seconds.flow.delta_ratio_percent.has_value()
        && one_minute.flow.delta_ratio_percent.has_value()) {
        result.delta_acceleration_points =
            *thirty_seconds.flow.delta_ratio_percent
            - *one_minute.flow.delta_ratio_percent;
    }
    if (thirty_seconds.flow.level1_ofi_ratio_percent.has_value()
        && one_minute.flow.level1_ofi_ratio_percent.has_value()) {
        result.ofi_acceleration_points =
            *thirty_seconds.flow.level1_ofi_ratio_percent
            - *one_minute.flow.level1_ofi_ratio_percent;
    }
    if (thirty_seconds.flow.delta_ratio_percent.has_value()
        && thirty_seconds.flow.level1_ofi_ratio_percent.has_value()) {
        result.combined_pressure_percent = std::clamp(
            0.60 * *thirty_seconds.flow.delta_ratio_percent
                + 0.40 * *thirty_seconds.flow.level1_ofi_ratio_percent,
            -100.0,
            100.0
        );
    } else if (thirty_seconds.flow.delta_ratio_percent.has_value()) {
        result.combined_pressure_percent =
            thirty_seconds.flow.delta_ratio_percent;
    } else {
        result.combined_pressure_percent =
            thirty_seconds.flow.level1_ofi_ratio_percent;
    }
    result.thirty_second_price_atr = price_response_atr(thirty_seconds, signal_atr);
    result.one_minute_price_atr = price_response_atr(one_minute, signal_atr);

    if (result.combined_pressure_percent.has_value()
        && result.thirty_second_price_atr.has_value()) {
        const double delta_30 = thirty_seconds.flow.delta_ratio_percent.value_or(0.0)
            / 100.0;
        const double delta_60 = one_minute.flow.delta_ratio_percent.value_or(0.0)
            / 100.0;
        const double ofi_30 = thirty_seconds.flow.level1_ofi_ratio_percent.value_or(0.0)
            / 100.0;
        const double ofi_60 = one_minute.flow.level1_ofi_ratio_percent.value_or(0.0)
            / 100.0;
        const double acceleration = std::clamp(delta_30 - delta_60, -1.0, 1.0);
        const double ofi_acceleration = std::clamp(ofi_30 - ofi_60, -1.0, 1.0);
        const double response = std::clamp(
            *result.thirty_second_price_atr
                / settings_.price_response_full_scale_atr,
            -1.0,
            1.0
        );
        const double microprice = std::clamp(
            thirty_seconds.flow.microprice_skew_basis_points.value_or(0.0)
                / settings_.microprice_full_scale_basis_points,
            -1.0,
            1.0
        );
        // The fixed weights keep each ingredient visible and bounded. Quality
        // shrinks weak tick samples toward zero instead of changing direction.
        const double raw_score = 100.0 * (
            0.25 * delta_30
            + 0.15 * delta_60
            + 0.10 * acceleration
            + 0.20 * ofi_30
            + 0.10 * ofi_60
            + 0.05 * ofi_acceleration
            + 0.05 * microprice
            + 0.10 * response
        );
        result.directional_score = std::clamp(
            raw_score * result.evidence_quality_percent / 100.0,
            -100.0,
            100.0
        );
    }

    if (result.thirty_second_price_atr.has_value()
        && thirty_seconds.flow.delta_ratio_percent.has_value()) {
        const double delta_fraction =
            std::abs(*thirty_seconds.flow.delta_ratio_percent) / 100.0;
        // Near-zero Delta makes division explode without adding directional
        // information. Balanced flow intentionally has no impact metric.
        if (std::abs(*thirty_seconds.flow.delta_ratio_percent)
            >= settings_.balanced_delta_ratio_percent) {
            result.normalized_impact_atr =
                *result.thirty_second_price_atr / delta_fraction;
        }
    }

    if (!thirty_seconds.complete || !one_minute.complete
        || !result.combined_pressure_percent.has_value()
        || !result.thirty_second_price_atr.has_value()) {
        return result;
    }

    const double delta = *result.combined_pressure_percent;
    const double response = *result.thirty_second_price_atr;
    if (std::abs(delta) < settings_.balanced_delta_ratio_percent) {
        result.pressure = domain::OrderFlowPressureState::balanced;
    } else if (delta > 0.0) {
        result.pressure = response > settings_.price_response_deadband_atr
            ? domain::OrderFlowPressureState::buying_effective
            : domain::OrderFlowPressureState::buying_absorbed;
    } else {
        result.pressure = response < -settings_.price_response_deadband_atr
            ? domain::OrderFlowPressureState::selling_effective
            : domain::OrderFlowPressureState::selling_absorbed;
    }
    return result;
}

} // namespace daytrader::analysis
