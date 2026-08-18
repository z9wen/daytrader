#pragma once

#include <chrono>
#include <deque>
#include <mutex>
#include <optional>

namespace daytrader::ibkr {

struct HistoricalRequestPacingSettings {
    // IBKR's published limit is 60/10 minutes; six units stay reserved for a
    // bar refresh performed by another historical-data component.
    int weighted_budget{54};
    std::chrono::milliseconds window{std::chrono::minutes{10}};
    std::chrono::milliseconds minimum_spacing{450};
};

// Process-wide sliding-window limiter for historical IBKR requests. BID_ASK
// callers acquire weight two because IBKR counts those requests twice.
class HistoricalRequestPacer {
public:
    explicit HistoricalRequestPacer(HistoricalRequestPacingSettings settings = {});

    [[nodiscard]] static HistoricalRequestPacer& shared();

    void acquire(int weight);

private:
    struct RequestStamp {
        std::chrono::steady_clock::time_point time;
        int weight{};
    };

    HistoricalRequestPacingSettings settings_;
    std::mutex mutex_;
    std::deque<RequestStamp> stamps_;
    std::optional<std::chrono::steady_clock::time_point> last_request_;
};

} // namespace daytrader::ibkr
