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
    config.monitoring.live_context_client_id = environment_integer(
        "DAYTRADER_LIVE_CLIENT_ID",
        config.ibkr.client_id + 1
    );
    config.monitoring.backfill_client_id = environment_integer(
        "DAYTRADER_BACKFILL_CLIENT_ID",
        config.ibkr.client_id + 2
    );
    config.monitoring.bar_interval = std::chrono::seconds{
        environment_integer(
            "DAYTRADER_BAR_INTERVAL_SECONDS",
            static_cast<int>(config.monitoring.bar_interval.count())
        )
    };
    config.monitoring.source_bar_interval = std::chrono::seconds{
        environment_integer(
            "DAYTRADER_SOURCE_BAR_INTERVAL_SECONDS",
            static_cast<int>(config.monitoring.source_bar_interval.count())
        )
    };
    config.monitoring.reconnect_delay = std::chrono::seconds{
        environment_integer(
            "DAYTRADER_RECONNECT_DELAY_SECONDS",
            static_cast<int>(config.monitoring.reconnect_delay.count())
        )
    };
    config.monitoring.history_duration = environment_string(
        "DAYTRADER_MONITOR_DURATION",
        config.monitoring.history_duration
    );
    config.monitoring.history_lookback_days = environment_integer(
        "DAYTRADER_MONITOR_LOOKBACK_DAYS",
        config.monitoring.history_lookback_days
    );
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
    config.minute_data_directory = environment_string(
        "DAYTRADER_MINUTE_DATA_DIR",
        config.minute_data_directory.string()
    );

    if (config.ibkr.port < 1 || config.ibkr.port > 65535) {
        throw std::invalid_argument("DAYTRADER_IBKR_PORT must be between 1 and 65535");
    }
    if (config.ibkr.client_id < 0) {
        throw std::invalid_argument("DAYTRADER_IBKR_CLIENT_ID must be non-negative");
    }
    if (config.monitoring.bar_interval <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("DAYTRADER_BAR_INTERVAL_SECONDS must be positive");
    }
    if (config.monitoring.source_bar_interval <= std::chrono::seconds::zero()
        || config.monitoring.bar_interval <= config.monitoring.source_bar_interval
        || config.monitoring.bar_interval.count()
            % config.monitoring.source_bar_interval.count() != 0) {
        throw std::invalid_argument(
            "DAYTRADER_SOURCE_BAR_INTERVAL_SECONDS must divide the trend interval"
        );
    }
    if (config.monitoring.reconnect_delay <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("DAYTRADER_RECONNECT_DELAY_SECONDS must be positive");
    }
    if (config.monitoring.history_duration.empty()) {
        throw std::invalid_argument("DAYTRADER_MONITOR_DURATION cannot be empty");
    }
    if (config.monitoring.history_lookback_days < 0) {
        throw std::invalid_argument(
            "DAYTRADER_MONITOR_LOOKBACK_DAYS must be zero (YTD) or positive"
        );
    }
    if (config.monitoring.live_context_client_id < 0
        || config.monitoring.live_context_client_id == config.ibkr.client_id) {
        throw std::invalid_argument(
            "DAYTRADER_LIVE_CLIENT_ID must be non-negative and differ from the bar client"
        );
    }
    if (config.monitoring.backfill_client_id < 0
        || config.monitoring.backfill_client_id == config.ibkr.client_id
        || config.monitoring.backfill_client_id
            == config.monitoring.live_context_client_id) {
        throw std::invalid_argument(
            "DAYTRADER_BACKFILL_CLIENT_ID must be non-negative and unique"
        );
    }
    if (config.minute_data_directory.empty()) {
        throw std::invalid_argument("DAYTRADER_MINUTE_DATA_DIR cannot be empty");
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
