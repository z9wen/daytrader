#include "daytrader/analysis/RelativeVolumeAnalyzer.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace daytrader::analysis {
namespace {

struct SessionVolume {
    double matching_bar{};
    double cumulative{};
    bool has_matching_bar{};
};

[[nodiscard]] double median(std::vector<double> values)
{
    std::ranges::sort(values);
    const auto middle = values.size() / 2;
    if (values.size() % 2 != 0) {
        return values[middle];
    }
    return (values[middle - 1] + values[middle]) / 2.0;
}

} // namespace

RelativeVolumeAnalyzer::RelativeVolumeAnalyzer(RelativeVolumeSettings settings)
    : settings_{settings}
{
    if (settings_.lookback_sessions == 0
        || settings_.minimum_baseline_sessions == 0
        || settings_.minimum_baseline_sessions > settings_.lookback_sessions
        || settings_.maximum_baseline_age_days == 0) {
        throw std::invalid_argument("relative-volume session counts are invalid");
    }
    if (!std::isfinite(settings_.light_threshold)
        || !std::isfinite(settings_.expanding_threshold)
        || settings_.light_threshold <= 0.0
        || settings_.light_threshold >= settings_.expanding_threshold) {
        throw std::invalid_argument("relative-volume thresholds are invalid");
    }
}

domain::RelativeVolumeSnapshot RelativeVolumeAnalyzer::analyze(
    std::span<const domain::MarketBar* const> bars,
    const time::TimeZoneFormatter& time_formatter
) const
{
    domain::RelativeVolumeSnapshot result;
    if (bars.empty() || !bars.back()->volume.has_value()
        || *bars.back()->volume <= 0.0) {
        return result;
    }

    const std::string current_date = time_formatter.format_date(
        bars.back()->epoch_seconds
    );
    const int current_slot = time_formatter.minutes_since_midnight(
        bars.back()->epoch_seconds
    );
    const auto oldest_baseline_timestamp = bars.back()->epoch_seconds
        - static_cast<std::int64_t>(settings_.maximum_baseline_age_days) * 86'400;
    std::map<std::string, SessionVolume> sessions;
    for (const auto* bar : bars) {
        if (bar->epoch_seconds < oldest_baseline_timestamp) {
            continue;
        }
        if (!bar->volume.has_value() || *bar->volume <= 0.0) {
            continue;
        }
        const auto date = time_formatter.format_date(bar->epoch_seconds);
        const int slot = time_formatter.minutes_since_midnight(bar->epoch_seconds);
        if (slot > current_slot) {
            continue;
        }
        auto& session = sessions[date];
        session.cumulative += *bar->volume;
        if (slot == current_slot) {
            session.matching_bar = *bar->volume;
            session.has_matching_bar = true;
        }
    }

    const auto current = sessions.find(current_date);
    if (current == sessions.end() || !current->second.has_matching_bar) {
        return result;
    }

    std::vector<double> prior_bars;
    std::vector<double> prior_cumulative;
    for (auto iterator = sessions.rbegin(); iterator != sessions.rend(); ++iterator) {
        if (iterator->first >= current_date || !iterator->second.has_matching_bar) {
            continue;
        }
        prior_bars.push_back(iterator->second.matching_bar);
        prior_cumulative.push_back(iterator->second.cumulative);
        if (prior_bars.size() == settings_.lookback_sessions) {
            break;
        }
    }
    result.baseline_sessions = prior_bars.size();
    if (prior_bars.size() < settings_.minimum_baseline_sessions) {
        return result;
    }

    const double normal_bar = median(prior_bars);
    const double normal_cumulative = median(prior_cumulative);
    if (normal_bar > 0.0) {
        result.bar_ratio = current->second.matching_bar / normal_bar;
    }
    if (normal_cumulative > 0.0) {
        result.cumulative_ratio = current->second.cumulative / normal_cumulative;
    }
    const auto reference = result.bar_ratio.has_value()
        ? result.bar_ratio
        : result.cumulative_ratio;
    if (!reference.has_value()) {
        return result;
    }
    if (*reference < settings_.light_threshold) {
        result.state = domain::RelativeVolumeState::light;
    } else if (*reference >= settings_.expanding_threshold) {
        result.state = domain::RelativeVolumeState::expanding;
    } else {
        result.state = domain::RelativeVolumeState::normal;
    }
    return result;
}

} // namespace daytrader::analysis
