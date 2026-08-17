#pragma once

#include <cstdint>
#include <string>

namespace daytrader::time {

// Formats epoch timestamps through the configured IANA zone (America/New_York by default).
class TimeZoneFormatter {
public:
    explicit TimeZoneFormatter(std::string zone_name);

    [[nodiscard]] std::string format(std::int64_t epoch_seconds) const;
    [[nodiscard]] std::string format_date(std::int64_t epoch_seconds) const;

private:
    std::string zone_name_;
};

} // namespace daytrader::time
