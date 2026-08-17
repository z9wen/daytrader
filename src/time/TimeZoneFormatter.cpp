#include "daytrader/time/TimeZoneFormatter.hpp"

#include <array>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace daytrader::time {
namespace {

std::mutex time_zone_mutex;

class ScopedProcessTimeZone {
public:
    explicit ScopedProcessTimeZone(const std::filesystem::path& zone_file)
    {
        if (const char* current = std::getenv("TZ"); current != nullptr) {
            previous_ = current;
        }

        const std::string value = ':' + zone_file.string();
        if (setenv("TZ", value.c_str(), 1) != 0) {
            throw std::runtime_error("unable to select the requested time zone");
        }
        tzset();
    }

    ~ScopedProcessTimeZone()
    {
        if (previous_.has_value()) {
            static_cast<void>(setenv("TZ", previous_->c_str(), 1));
        } else {
            static_cast<void>(unsetenv("TZ"));
        }
        tzset();
    }

    ScopedProcessTimeZone(const ScopedProcessTimeZone&) = delete;
    ScopedProcessTimeZone& operator=(const ScopedProcessTimeZone&) = delete;

private:
    std::optional<std::string> previous_;
};

[[nodiscard]] std::filesystem::path zone_file_for(const std::string& zone_name)
{
    if (zone_name.empty() || zone_name.starts_with('/') || zone_name.find("..") != std::string::npos) {
        throw std::invalid_argument("invalid IANA time-zone name: " + zone_name);
    }

    const auto path = std::filesystem::path{"/usr/share/zoneinfo"} / zone_name;
    if (!std::filesystem::is_regular_file(path)) {
        throw std::invalid_argument("unknown IANA time zone: " + zone_name);
    }
    return path;
}

[[nodiscard]] std::tuple<std::string, std::string, int> format_all_in_zone(
    const std::string& zone_name,
    std::int64_t epoch_seconds
)
{
    const std::lock_guard lock{time_zone_mutex};
    const ScopedProcessTimeZone selected_zone{zone_file_for(zone_name)};

    const std::time_t timestamp = static_cast<std::time_t>(epoch_seconds);
    std::tm local_time{};
    if (localtime_r(&timestamp, &local_time) == nullptr) {
        throw std::runtime_error("unable to convert market-data timestamp");
    }

    std::array<char, 32> full{};
    std::array<char, 16> date{};
    if (std::strftime(full.data(), full.size(), "%Y-%m-%d %H:%M:%S %Z", &local_time) == 0
        || std::strftime(date.data(), date.size(), "%Y-%m-%d", &local_time) == 0) {
        throw std::runtime_error("unable to format market-data timestamp");
    }
    return {
        std::string{full.data()},
        std::string{date.data()},
        local_time.tm_hour * 60 + local_time.tm_min,
    };
}

} // namespace

TimeZoneFormatter::TimeZoneFormatter(std::string zone_name)
    : zone_name_{std::move(zone_name)}
{
    static_cast<void>(zone_file_for(zone_name_));
}

const TimeZoneFormatter::CachedTime& TimeZoneFormatter::cached_time(
    std::int64_t epoch_seconds
) const
{
    const auto found = cache_.find(epoch_seconds);
    if (found != cache_.end()) {
        return found->second;
    }
    return cache_.emplace(
        epoch_seconds,
        format_all_in_zone(zone_name_, epoch_seconds)
    ).first->second;
}

std::string TimeZoneFormatter::format(std::int64_t epoch_seconds) const
{
    return std::get<0>(cached_time(epoch_seconds));
}

std::string TimeZoneFormatter::format_date(std::int64_t epoch_seconds) const
{
    return std::get<1>(cached_time(epoch_seconds));
}

int TimeZoneFormatter::minutes_since_midnight(std::int64_t epoch_seconds) const
{
    return std::get<2>(cached_time(epoch_seconds));
}

} // namespace daytrader::time
