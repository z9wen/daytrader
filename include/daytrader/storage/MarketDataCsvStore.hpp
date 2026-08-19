#pragma once

#include "daytrader/domain/InstrumentBars.hpp"

#include <chrono>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace daytrader::storage {

using CompletedMarketSessions =
    std::unordered_map<std::string, std::unordered_set<std::string>>;

// Durable, human-readable cache for normalized IBKR bars. One CSV per symbol
// lets later research add instruments without rewriting an unrelated dataset.
class MarketDataCsvStore {
public:
    explicit MarketDataCsvStore(std::filesystem::path directory);

    [[nodiscard]] std::vector<domain::InstrumentBars> load(
        std::span<const std::string> symbols
    ) const;

    // Replaces each symbol file with its sorted, deduplicated merged history.
    void save(std::span<const domain::InstrumentBars> instruments) const;

    // File age is used only to decide whether a small recent-data refresh is
    // worthwhile; historical coverage is checked independently from bar times.
    [[nodiscard]] bool is_recent(
        std::span<const std::string> symbols,
        std::chrono::hours maximum_age
    ) const;

    // Successful daily requests are tracked separately from bar count because
    // less-active ETFs can legitimately have no trade during some extended-
    // hours minutes. This makes interrupted bulk downloads exactly resumable.
    [[nodiscard]] CompletedMarketSessions load_completed_sessions() const;

    void mark_session_complete(
        std::string_view symbol,
        std::string_view market_date
    ) const;

    [[nodiscard]] const std::filesystem::path& directory() const noexcept;

private:
    [[nodiscard]] std::filesystem::path path_for(const std::string& symbol) const;
    [[nodiscard]] std::filesystem::path completed_sessions_path() const;

    std::filesystem::path directory_;
};

} // namespace daytrader::storage
