#pragma once

#include "daytrader/domain/TradingSchedule.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace daytrader::storage {

// Durable IBKR exchange-session calendar partitioned as SYMBOL/YEAR.csv.
// This calendar is deliberately separate from OHLCV cache completion state.
class TradingScheduleCsvStore {
public:
    explicit TradingScheduleCsvStore(std::filesystem::path directory);

    [[nodiscard]] bool has_year(const std::string& symbol, int year) const;

    [[nodiscard]] bool covers_through(
        const std::string& symbol,
        int year,
        std::string_view market_date
    ) const;

    [[nodiscard]] domain::TradingSchedule load_year(
        const std::string& symbol,
        int year
    ) const;

    [[nodiscard]] domain::TradingSchedule load_range(
        const std::string& symbol,
        std::string_view first_market_date,
        std::string_view last_market_date
    ) const;

    // Replaces one year atomically after sorting and de-duplicating refDate.
    void save_year(
        const std::string& symbol,
        int year,
        std::span<const domain::TradingSession> sessions,
        std::string_view covered_through
    ) const;

    [[nodiscard]] const std::filesystem::path& directory() const noexcept;

private:
    [[nodiscard]] std::filesystem::path path_for(
        const std::string& symbol,
        int year
    ) const;
    [[nodiscard]] std::filesystem::path coverage_path_for(
        const std::string& symbol,
        int year
    ) const;

    std::filesystem::path directory_;
};

} // namespace daytrader::storage
