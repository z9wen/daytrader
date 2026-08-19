#include "daytrader/ibkr/IbkrErrorClassifier.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace daytrader::ibkr {

namespace {

[[nodiscard]] std::string normalize(std::string_view message)
{
    std::string normalized{message};
    std::ranges::transform(normalized, normalized.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return normalized;
}

} // namespace

bool is_pacing_or_rate_limit_error(std::string_view message)
{
    const auto normalized = normalize(message);
    return normalized.find("pacing") != std::string::npos
        || normalized.find("rate limit") != std::string::npos
        || normalized.find("max rate") != std::string::npos
        || normalized.find("too many request") != std::string::npos;
}

bool is_pacing_or_rate_limit_error(int error_code, std::string_view message)
{
    // Current official error code 100: max messages per second exceeded.
    // Historical-data pacing violations may arrive under a service code with
    // the pacing explanation in its text, so retain the textual fallback.
    return error_code == 100 || is_pacing_or_rate_limit_error(message);
}

bool is_market_data_capacity_error(int error_code)
{
    return error_code == 101;
}

bool is_connection_interruption_error(std::string_view message)
{
    const auto normalized = normalize(message);
    return normalized.find("couldn't connect") != std::string::npos
        || normalized.find("unable to connect") != std::string::npos
        || normalized.find("connection closed") != std::string::npos
        || normalized.find("not connected") != std::string::npos;
}

} // namespace daytrader::ibkr
