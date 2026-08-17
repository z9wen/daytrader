#pragma once

#include <cstdint>
#include <string>
#include <tuple>
#include <unordered_map>

namespace daytrader::time {

// Formats epoch timestamps through the configured IANA zone (America/New_York by default).
class TimeZoneFormatter {
public:
    explicit TimeZoneFormatter(std::string zone_name);

    [[nodiscard]] std::string format(std::int64_t epoch_seconds) const;
    [[nodiscard]] std::string format_date(std::int64_t epoch_seconds) const;
    [[nodiscard]] int minutes_since_midnight(std::int64_t epoch_seconds) const;

private:
    using CachedTime = std::tuple<std::string, std::string, int>;

    [[nodiscard]] const CachedTime& cached_time(std::int64_t epoch_seconds) const;

    std::string zone_name_;
    mutable std::unordered_map<std::int64_t, CachedTime> cache_;
};

} // namespace daytrader::time
