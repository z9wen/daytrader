#include "daytrader/config/AppConfig.hpp"

#include "daytrader/universe/EtfUniverse.hpp"

#include <charconv>
#include <cstdlib>
#include <stdexcept>
#include <string_view>

namespace daytrader::config {
namespace {

[[nodiscard]] std::string environment_string(const char* name, std::string fallback)
{
    if (const char* value = std::getenv(name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

[[nodiscard]] int environment_integer(const char* name, int fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }

    int parsed{};
    const std::string_view text{value};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument(std::string{name} + " must be an integer");
    }
    return parsed;
}

} // namespace

AppConfig AppConfig::from_environment()
{
    AppConfig config;
    config.etfs = universe::default_etf_universe();
    config.ibkr.host = environment_string("DAYTRADER_IBKR_HOST", config.ibkr.host);
    config.ibkr.port = environment_integer("DAYTRADER_IBKR_PORT", config.ibkr.port);
    config.ibkr.client_id = environment_integer("DAYTRADER_IBKR_CLIENT_ID", config.ibkr.client_id);
    config.ibkr.request_timeout = std::chrono::seconds{
        environment_integer(
            "DAYTRADER_REQUEST_TIMEOUT_SECONDS",
            static_cast<int>(config.ibkr.request_timeout.count())
        )
    };
    config.monitoring.bar_interval = std::chrono::seconds{
        environment_integer(
            "DAYTRADER_BAR_INTERVAL_SECONDS",
            static_cast<int>(config.monitoring.bar_interval.count())
        )
    };
    config.monitoring.reconnect_delay = std::chrono::seconds{
        environment_integer(
            "DAYTRADER_RECONNECT_DELAY_SECONDS",
            static_cast<int>(config.monitoring.reconnect_delay.count())
        )
    };

    for (auto& etf : config.etfs) {
        auto& request = etf.market_data;
        request.data_type = environment_string("DAYTRADER_DATA_TYPE", request.data_type);
        request.duration = environment_string("DAYTRADER_DURATION", request.duration);
        request.bar_size = environment_string("DAYTRADER_BAR_SIZE", request.bar_size);
        request.end_delay = std::chrono::minutes{
            environment_integer(
                "DAYTRADER_HISTORICAL_DELAY_MINUTES",
                static_cast<int>(request.end_delay.count())
            )
        };
    }
    config.time_zone = environment_string("DAYTRADER_TIME_ZONE", config.time_zone);

    if (config.ibkr.port < 1 || config.ibkr.port > 65535) {
        throw std::invalid_argument("DAYTRADER_IBKR_PORT must be between 1 and 65535");
    }
    if (config.ibkr.client_id < 0) {
        throw std::invalid_argument("DAYTRADER_IBKR_CLIENT_ID must be non-negative");
    }
    if (config.ibkr.request_timeout <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("DAYTRADER_REQUEST_TIMEOUT_SECONDS must be positive");
    }
    if (config.monitoring.bar_interval <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("DAYTRADER_BAR_INTERVAL_SECONDS must be positive");
    }
    if (config.monitoring.reconnect_delay <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("DAYTRADER_RECONNECT_DELAY_SECONDS must be positive");
    }
    for (const auto& etf : config.etfs) {
        const auto& request = etf.market_data;
        if (request.end_delay < std::chrono::minutes::zero()) {
            throw std::invalid_argument("DAYTRADER_HISTORICAL_DELAY_MINUTES cannot be negative");
        }
    }

    return config;
}

} // namespace daytrader::config
