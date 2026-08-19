#include "daytrader/analysis/SetupCalibrationEngine.hpp"

#include "daytrader/storage/SetupOutcomeCsvStore.hpp"
#include "daytrader/time/TimeZoneFormatter.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace daytrader::analysis {
namespace {

struct CandidateObservation {
    std::string signal_symbol;
    std::string trade_symbol;
    domain::SetupKind kind{domain::SetupKind::building};
    bool active{};
    int bullish_score{};
    double entry_price{};
    double atr{};
    std::optional<double> relative_volume;
    domain::RelativeStrengthHorizons rs_spy;
    domain::RelativeStrengthHorizons rs_qqq;
    std::optional<double> delta30;
    std::optional<double> ofi30;
    std::optional<double> combined_pressure;
    std::optional<double> spread_basis_points;
};

[[nodiscard]] std::string wave_key(
    const std::string& signal_symbol,
    domain::SetupKind kind
)
{
    return signal_symbol + ':' + std::string{domain::to_string(kind)};
}

[[nodiscard]] std::optional<double> rvol_value(
    const domain::RelativeVolumeSnapshot& volume
)
{
    return volume.bar_ratio.has_value() ? volume.bar_ratio : volume.cumulative_ratio;
}

[[nodiscard]] const domain::LiveOrderFlowSnapshot* flow_for(
    const domain::MarketScan& scan,
    const std::string& symbol
)
{
    const auto found = std::ranges::find(
        scan.live_context.order_flow,
        symbol,
        &domain::LiveOrderFlowSnapshot::symbol
    );
    return found == scan.live_context.order_flow.end() ? nullptr : &*found;
}

void add_flow_features(
    CandidateObservation& observation,
    const domain::LiveOrderFlowSnapshot* flow
)
{
    if (flow == nullptr) {
        return;
    }
    observation.delta30 = flow->thirty_seconds.flow.delta_ratio_percent;
    observation.ofi30 = flow->thirty_seconds.flow.level1_ofi_ratio_percent;
    observation.spread_basis_points =
        flow->thirty_seconds.flow.average_spread_basis_points;
    if (flow->assessment.has_value()) {
        observation.combined_pressure = flow->assessment->combined_pressure_percent;
    }
}

[[nodiscard]] std::vector<CandidateObservation> observations_for(
    const domain::MarketScan& scan
)
{
    std::vector<CandidateObservation> observations;
    const auto add_rankings = [&](const std::vector<domain::RankedEtf>& rankings) {
        for (const auto& rank : rankings) {
            const auto* zone = !rank.leveraged_long_symbol.empty()
                    && rank.leveraged_entry_zone.has_value()
                ? &*rank.leveraged_entry_zone
                : (rank.entry_zone.has_value() ? &*rank.entry_zone : nullptr);
            const std::string trade_symbol = !rank.leveraged_long_symbol.empty()
                ? rank.leveraged_long_symbol
                : rank.symbol;
            for (const auto kind : {domain::SetupKind::building,
                                    domain::SetupKind::ready}) {
                CandidateObservation observation{
                    .signal_symbol = rank.symbol,
                    .trade_symbol = trade_symbol,
                    .kind = kind,
                    .active = kind == domain::SetupKind::building
                        ? rank.long_opportunity.phase == domain::BullishPhase::building
                        : rank.leveraged_execution.entry
                            == domain::LongEntryDecision::ready,
                    .bullish_score = rank.long_opportunity.bullish_score,
                    .entry_price = zone == nullptr ? 0.0 : zone->current_price,
                    .atr = zone == nullptr ? 0.0 : zone->atr14,
                    .relative_volume = rvol_value(rank.relative_volume),
                    .rs_spy = rank.relative_strength_vs_spy,
                    .rs_qqq = rank.relative_strength_vs_qqq,
                };
                add_flow_features(observation, flow_for(scan, rank.symbol));
                observations.push_back(std::move(observation));
            }
        }
    };
    add_rankings(scan.sector_rankings);
    add_rankings(scan.rankings);

    const auto* tqqq_zone = scan.tqqq_entry_zone.has_value()
        ? &*scan.tqqq_entry_zone
        : nullptr;
    const bool qqq_building = scan.qqq.trend_signal == domain::MarketTrendSignal::neutral
        && scan.qqq.session_vwap.has_value()
        && scan.qqq.close >= *scan.qqq.session_vwap
        && scan.qqq.ema20_change_percent > 0.0;
    for (const auto kind : {domain::SetupKind::building, domain::SetupKind::ready}) {
        CandidateObservation observation{
            .signal_symbol = "QQQ",
            .trade_symbol = "TQQQ",
            .kind = kind,
            .active = kind == domain::SetupKind::building
                ? qqq_building
                : scan.tqqq_execution.has_value()
                    && scan.tqqq_execution->entry == domain::LongEntryDecision::ready,
            .bullish_score = scan.qqq.trend_signal == domain::MarketTrendSignal::strong
                ? 80
                : 50,
            .entry_price = tqqq_zone == nullptr ? 0.0 : tqqq_zone->current_price,
            .atr = tqqq_zone == nullptr ? 0.0 : tqqq_zone->atr14,
            .relative_volume = rvol_value(scan.qqq.relative_volume),
        };
        add_flow_features(observation, flow_for(scan, "QQQ"));
        observations.push_back(std::move(observation));
    }
    return observations;
}

[[nodiscard]] bool is_resolved(domain::SetupOutcome outcome)
{
    return outcome == domain::SetupOutcome::success
        || outcome == domain::SetupOutcome::failure;
}

struct OutcomeCounts {
    std::size_t samples{};
    std::size_t successes{};
};

template <typename Predicate>
[[nodiscard]] OutcomeCounts count_outcomes(
    const std::vector<domain::SetupOutcomeRecord>& records,
    Predicate predicate
)
{
    OutcomeCounts counts;
    for (const auto& record : records) {
        if (!is_resolved(record.outcome) || !predicate(record)) {
            continue;
        }
        ++counts.samples;
        counts.successes += record.outcome == domain::SetupOutcome::success ? 1 : 0;
    }
    return counts;
}

[[nodiscard]] domain::SetupProbabilityEstimate make_estimate(
    OutcomeCounts counts,
    domain::CalibrationScope scope
)
{
    // Beta(1,1) smoothing keeps tiny datasets honest without suppressing them.
    const double posterior = static_cast<double>(counts.successes + 1)
        / static_cast<double>(counts.samples + 2);
    const double observed = counts.samples == 0
        ? 0.5
        : static_cast<double>(counts.successes) / static_cast<double>(counts.samples);
    const double z = 1.96;
    const double n = static_cast<double>(counts.samples);
    double lower{};
    double upper{1.0};
    if (counts.samples > 0) {
        const double denominator = 1.0 + z * z / n;
        const double center = (observed + z * z / (2.0 * n)) / denominator;
        const double radius = z * std::sqrt(
            observed * (1.0 - observed) / n + z * z / (4.0 * n * n)
        ) / denominator;
        lower = std::max(0.0, center - radius);
        upper = std::min(1.0, center + radius);
    }
    return domain::SetupProbabilityEstimate{
        .success_probability_percent = posterior * 100.0,
        .lower_confidence_percent = lower * 100.0,
        .upper_confidence_percent = upper * 100.0,
        .samples = counts.samples,
        .successes = counts.successes,
        .scope = scope,
    };
}

} // namespace

struct SetupCalibrationEngine::Impl {
    storage::SetupOutcomeCsvStore store;
    time::TimeZoneFormatter formatter;
    SetupCalibrationSettings settings;
    std::vector<domain::SetupOutcomeRecord> records;
    std::unordered_map<std::string, bool> active_waves;
    mutable std::mutex mutex;

    Impl(
        std::filesystem::path cache_path,
        std::string time_zone,
        SetupCalibrationSettings calibration_settings
    )
        : store{std::move(cache_path)}
        , formatter{std::move(time_zone)}
        , settings{calibration_settings}
        , records{store.load()}
    {
        if (settings.outcome_horizon <= std::chrono::minutes::zero()
            || !std::isfinite(settings.favorable_target_atr)
            || !std::isfinite(settings.adverse_stop_atr)
            || settings.favorable_target_atr <= 0.0
            || settings.adverse_stop_atr <= 0.0) {
            throw std::invalid_argument("setup calibration settings are invalid");
        }
    }

    [[nodiscard]] bool regular_session(std::int64_t timestamp) const
    {
        const int minute = formatter.minutes_since_midnight(timestamp);
        return minute >= 9 * 60 + 30 && minute < 16 * 60;
    }

    [[nodiscard]] std::optional<domain::SetupProbabilityEstimate> estimate(
        const std::string& signal_symbol,
        domain::SetupKind kind,
        bool rth,
        int score
    ) const
    {
        const int bucket = std::clamp(score, 0, 100) / 10;
        auto counts = count_outcomes(records, [&](const auto& record) {
            return record.signal_symbol == signal_symbol && record.kind == kind
                && record.regular_session == rth
                && std::clamp(record.bullish_score, 0, 100) / 10 == bucket;
        });
        auto scope = domain::CalibrationScope::score_bucket;
        if (counts.samples == 0) {
            counts = count_outcomes(records, [&](const auto& record) {
                return record.signal_symbol == signal_symbol && record.kind == kind
                    && record.regular_session == rth;
            });
            scope = domain::CalibrationScope::symbol;
        }
        if (counts.samples == 0) {
            counts = count_outcomes(records, [&](const auto& record) {
                return record.kind == kind && record.regular_session == rth;
            });
            scope = domain::CalibrationScope::session;
        }
        if (counts.samples == 0) {
            counts = count_outcomes(records, [&](const auto& record) {
                return record.kind == kind;
            });
            scope = domain::CalibrationScope::global;
        }
        if (counts.samples == 0) {
            return std::nullopt;
        }
        return make_estimate(counts, scope);
    }

    bool resolve_pending(std::span<const domain::InstrumentBars> history)
    {
        bool changed{};
        for (auto& record : records) {
            if (record.outcome != domain::SetupOutcome::pending) {
                continue;
            }
            const auto instrument = std::ranges::find(
                history,
                record.trade_symbol,
                &domain::InstrumentBars::symbol
            );
            if (instrument == history.end() || instrument->bars.empty()) {
                continue;
            }
            const auto deadline = record.observed_epoch_seconds
                + std::chrono::duration_cast<std::chrono::seconds>(
                    settings.outcome_horizon
                ).count();
            const auto first_future = std::ranges::upper_bound(
                instrument->bars,
                record.observed_epoch_seconds,
                {},
                &domain::MarketBar::epoch_seconds
            );
            for (auto iterator = first_future;
                 iterator != instrument->bars.end(); ++iterator) {
                const auto& bar = *iterator;
                if (bar.epoch_seconds > deadline) {
                    break;
                }
                record.maximum_favorable_excursion_atr = std::max(
                    record.maximum_favorable_excursion_atr,
                    (bar.high - record.entry_price) / record.atr
                );
                record.maximum_adverse_excursion_atr = std::max(
                    record.maximum_adverse_excursion_atr,
                    (record.entry_price - bar.low) / record.atr
                );
                const bool target_hit = bar.high >= record.target_price;
                const bool stop_hit = bar.low <= record.stop_price;
                if (target_hit && stop_hit) {
                    record.outcome = domain::SetupOutcome::ambiguous;
                } else if (target_hit) {
                    record.outcome = domain::SetupOutcome::success;
                    record.lead_seconds = bar.epoch_seconds
                        - record.observed_epoch_seconds;
                } else if (stop_hit) {
                    record.outcome = domain::SetupOutcome::failure;
                }
                if (record.outcome != domain::SetupOutcome::pending) {
                    record.resolved_epoch_seconds = bar.epoch_seconds;
                    changed = true;
                    break;
                }
            }
            if (record.outcome == domain::SetupOutcome::pending
                && instrument->bars.back().epoch_seconds >= deadline) {
                record.outcome = domain::SetupOutcome::failure;
                record.resolved_epoch_seconds = deadline;
                changed = true;
            }
        }
        return changed;
    }

    bool start_new_waves(
        const domain::MarketScan& scan,
        const std::vector<CandidateObservation>& observations
    )
    {
        bool changed{};
        for (const auto& observation : observations) {
            const auto key = wave_key(observation.signal_symbol, observation.kind);
            const bool was_active = active_waves[key];
            active_waves[key] = observation.active;
            if (!observation.active || was_active || observation.entry_price <= 0.0
                || observation.atr <= 0.0) {
                continue;
            }
            const auto horizon_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                settings.outcome_horizon
            ).count();
            // A restart loses the in-memory edge state. Treat a recent durable
            // observation as the same wave so reconnecting cannot manufacture
            // another calibration sample from an uninterrupted setup.
            const bool already_observed = std::ranges::any_of(
                records,
                [&](const auto& record) {
                    if (record.signal_symbol != observation.signal_symbol
                        || record.kind != observation.kind) {
                        return false;
                    }
                    return record.outcome == domain::SetupOutcome::pending
                        || (record.observed_epoch_seconds <= scan.epoch_seconds
                            && record.observed_epoch_seconds
                                >= scan.epoch_seconds - horizon_seconds);
                }
            );
            if (already_observed) {
                continue;
            }
            records.push_back(domain::SetupOutcomeRecord{
                .observed_epoch_seconds = scan.epoch_seconds,
                .signal_symbol = observation.signal_symbol,
                .trade_symbol = observation.trade_symbol,
                .kind = observation.kind,
                .regular_session = regular_session(scan.epoch_seconds),
                .bullish_score = observation.bullish_score,
                .entry_price = observation.entry_price,
                .atr = observation.atr,
                .target_price = observation.entry_price
                    + settings.favorable_target_atr * observation.atr,
                .stop_price = observation.entry_price
                    - settings.adverse_stop_atr * observation.atr,
                .relative_volume = observation.relative_volume,
                .rs15_spy = observation.rs_spy.fifteen_minute_percent,
                .rs30_spy = observation.rs_spy.thirty_minute_percent,
                .rs60_spy = observation.rs_spy.sixty_minute_percent,
                .rs15_qqq = observation.rs_qqq.fifteen_minute_percent,
                .rs30_qqq = observation.rs_qqq.thirty_minute_percent,
                .rs60_qqq = observation.rs_qqq.sixty_minute_percent,
                .delta30 = observation.delta30,
                .ofi30 = observation.ofi30,
                .combined_pressure = observation.combined_pressure,
                .spread_basis_points = observation.spread_basis_points,
            });
            changed = true;
        }
        return changed;
    }

    void attach(domain::MarketScan& scan) const
    {
        const bool rth = regular_session(scan.epoch_seconds);
        const auto attach_rankings = [&](std::vector<domain::RankedEtf>& rankings) {
            for (auto& rank : rankings) {
                rank.long_opportunity.setup_probability.reset();
                rank.leveraged_execution.setup_probability.reset();
                if (rank.long_opportunity.phase == domain::BullishPhase::building) {
                    rank.long_opportunity.setup_probability = estimate(
                        rank.symbol,
                        domain::SetupKind::building,
                        rth,
                        rank.long_opportunity.bullish_score
                    );
                }
                if (rank.leveraged_execution.entry
                    == domain::LongEntryDecision::ready) {
                    rank.leveraged_execution.setup_probability = estimate(
                        rank.symbol,
                        domain::SetupKind::ready,
                        rth,
                        rank.long_opportunity.bullish_score
                    );
                }
            }
        };
        attach_rankings(scan.sector_rankings);
        attach_rankings(scan.rankings);
        scan.qqq_building_probability.reset();
        const bool qqq_building = scan.qqq.trend_signal
                == domain::MarketTrendSignal::neutral
            && scan.qqq.session_vwap.has_value()
            && scan.qqq.close >= *scan.qqq.session_vwap
            && scan.qqq.ema20_change_percent > 0.0;
        if (qqq_building) {
            scan.qqq_building_probability = estimate(
                "QQQ",
                domain::SetupKind::building,
                rth,
                50
            );
        }
        if (scan.tqqq_execution.has_value()) {
            scan.tqqq_execution->setup_probability.reset();
            if (scan.tqqq_execution->entry == domain::LongEntryDecision::ready) {
                scan.tqqq_execution->setup_probability = estimate(
                    "QQQ",
                    domain::SetupKind::ready,
                    rth,
                    scan.qqq.trend_signal == domain::MarketTrendSignal::strong ? 80 : 50
                );
            }
        }
    }
};

SetupCalibrationEngine::SetupCalibrationEngine(
    std::filesystem::path cache_path,
    std::string time_zone,
    SetupCalibrationSettings settings
)
    : impl_{std::make_unique<Impl>(
        std::move(cache_path),
        std::move(time_zone),
        settings
    )}
{
}

SetupCalibrationEngine::~SetupCalibrationEngine() = default;

void SetupCalibrationEngine::observe_and_enrich(
    domain::MarketScan& scan,
    std::span<const domain::InstrumentBars> minute_history
)
{
    const std::lock_guard lock{impl_->mutex};
    bool changed = impl_->resolve_pending(minute_history);
    const auto observations = observations_for(scan);
    changed = impl_->start_new_waves(scan, observations) || changed;
    impl_->attach(scan);
    if (changed) {
        impl_->store.save(impl_->records);
    }
}

void SetupCalibrationEngine::enrich(domain::MarketScan& scan) const
{
    const std::lock_guard lock{impl_->mutex};
    impl_->attach(scan);
}

std::size_t SetupCalibrationEngine::record_count() const
{
    const std::lock_guard lock{impl_->mutex};
    return impl_->records.size();
}

} // namespace daytrader::analysis
