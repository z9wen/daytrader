#include "daytrader/ibkr/HistoricalRequestPacer.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace daytrader::ibkr {

HistoricalRequestPacer::HistoricalRequestPacer(
    HistoricalRequestPacingSettings settings
)
    : settings_{settings}
{
    if (settings_.weighted_budget <= 0) {
        throw std::invalid_argument("historical request budget must be positive");
    }
    if (settings_.window <= std::chrono::milliseconds::zero()
        || settings_.minimum_spacing < std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("historical request pacing durations are invalid");
    }
}

HistoricalRequestPacer& HistoricalRequestPacer::shared()
{
    static HistoricalRequestPacer pacer;
    return pacer;
}

void HistoricalRequestPacer::acquire(int weight)
{
    if (weight <= 0 || weight > settings_.weighted_budget) {
        throw std::invalid_argument("invalid historical request pacing weight");
    }

    while (true) {
        std::chrono::steady_clock::duration required_wait{};
        {
            std::lock_guard lock{mutex_};
            const auto now = std::chrono::steady_clock::now();
            while (!stamps_.empty()
                   && now - stamps_.front().time >= settings_.window) {
                stamps_.pop_front();
            }

            auto ready_at = now;
            if (last_request_.has_value()) {
                ready_at = std::max(
                    ready_at,
                    *last_request_ + settings_.minimum_spacing
                );
            }

            int used_weight{};
            for (const auto& stamp : stamps_) {
                used_weight += stamp.weight;
            }
            if (used_weight + weight > settings_.weighted_budget) {
                int expiring_weight{};
                for (const auto& stamp : stamps_) {
                    expiring_weight += stamp.weight;
                    if (used_weight - expiring_weight + weight
                        <= settings_.weighted_budget) {
                        ready_at = std::max(
                            ready_at,
                            stamp.time + settings_.window
                                + std::chrono::milliseconds{100}
                        );
                        break;
                    }
                }
            }

            if (ready_at <= now) {
                stamps_.push_back({.time = now, .weight = weight});
                last_request_ = now;
                return;
            }
            required_wait = ready_at - now;
        }

        if (required_wait >= std::chrono::seconds{1}) {
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                required_wait + std::chrono::milliseconds{999}
            ).count();
            std::clog << "IBKR historical pacing: waiting " << seconds
                      << " seconds; completed tick windows are already cached\n";
        }
        std::this_thread::sleep_for(required_wait);
    }
}

} // namespace daytrader::ibkr
