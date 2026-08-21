#include "daytrader/storage/TradingScheduleCsvStore.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] daytrader::domain::TradingSession session(
    std::string date,
    std::string close = "20260102 16:00:00 US/Eastern"
)
{
    return daytrader::domain::TradingSession{
        .market_date = std::move(date),
        .start_datetime = "20260102 09:30:00 US/Eastern",
        .end_datetime = std::move(close),
        .time_zone = "US/Eastern",
    };
}

void annual_schedule_round_trip_is_sorted_and_replaceable()
{
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path()
        / ("daytrader-trading-schedule-" + std::to_string(unique));
    const daytrader::storage::TradingScheduleCsvStore store{directory};

    const std::vector<daytrader::domain::TradingSession> initial{
        session("2026-01-05"),
        session("2026-01-02"),
        session("2026-01-02"),
    };
    store.save_year("SPY", 2026, initial, "2026-01-05");
    require(store.has_year("SPY", 2026), "saved schedule year should exist");
    require(
        store.covers_through("SPY", 2026, "2026-01-05"),
        "coverage marker should include its requested endpoint"
    );
    require(
        !store.covers_through("SPY", 2026, "2026-12-31"),
        "a partial current year must not appear complete after year rollover"
    );
    const auto loaded = store.load_year("SPY", 2026);
    require(loaded.sessions.size() == 2, "duplicate refDate should be removed");
    require(
        loaded.sessions.front().market_date == "2026-01-02",
        "schedule should be stored chronologically"
    );

    const std::vector<daytrader::domain::TradingSession> refreshed{
        session("2026-01-02", "20260102 13:00:00 US/Eastern"),
        session("2026-01-06"),
    };
    store.save_year("SPY", 2026, refreshed, "2026-01-06");
    const auto replaced = store.load_range("SPY", "2026-01-01", "2026-01-05");
    require(replaced.sessions.size() == 1, "save_year should replace old coverage");
    require(
        replaced.sessions[0].end_datetime == "20260102 13:00:00 US/Eastern",
        "IBKR early-close time should round-trip"
    );

    std::filesystem::remove_all(directory);
}

void rejects_a_session_in_the_wrong_year_partition()
{
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path()
        / ("daytrader-trading-schedule-invalid-" + std::to_string(unique));
    const daytrader::storage::TradingScheduleCsvStore store{directory};
    bool threw{};
    try {
        const std::vector<daytrader::domain::TradingSession> wrong{
            session("2025-12-31"),
        };
        store.save_year("SPY", 2026, wrong, "2026-12-31");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "wrong-year session should be rejected");
    std::filesystem::remove_all(directory);
}

} // namespace

int main()
{
    try {
        annual_schedule_round_trip_is_sorted_and_replaceable();
        rejects_a_session_in_the_wrong_year_partition();
        std::cout << "TradingScheduleCsvStoreTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "TradingScheduleCsvStoreTests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
