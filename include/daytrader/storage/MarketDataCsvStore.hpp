#pragma once

#include "daytrader/domain/InstrumentBars.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace daytrader::storage {

using CompletedMarketSessions =
    std::unordered_map<std::string, std::unordered_set<std::string>>;

struct LegacyMigrationReport {
    std::size_t symbols{};
    std::size_t bars{};
    std::size_t year_partitions{};
    std::filesystem::path archive_directory;
};

// Durable, human-readable cache for normalized IBKR bars. New data is
// partitioned as SYMBOL/YEAR.csv so long history can be updated without
// rewriting every year. Legacy SYMBOL.csv files remain readable and are
// superseded one year at a time as partition files are created.
class MarketDataCsvStore {
public:
    explicit MarketDataCsvStore(std::filesystem::path directory);

    [[nodiscard]] std::vector<domain::InstrumentBars> load(
        std::span<const std::string> symbols
    ) const;

    // Loads only bars at or after the requested epoch. This keeps the live
    // monitor from materializing a multi-year research cache on every start.
    [[nodiscard]] std::vector<domain::InstrumentBars> load_since(
        std::span<const std::string> symbols,
        std::int64_t first_epoch_seconds
    ) const;

    // Replaces every symbol/year partition represented by the input bars.
    // Unrelated years are left untouched.
    void save(std::span<const domain::InstrumentBars> instruments) const;

    // Atomically merges incremental bars into only the affected year
    // partitions. Incoming values replace an older bar with the same timestamp.
    void merge(std::span<const domain::InstrumentBars> instruments) const;

    // Converts every legacy SYMBOL.csv in the cache root to SYMBOL/YEAR.csv.
    // Each source file is verified, then moved to legacy_flat/ as a recoverable
    // backup. The completion manifest is not moved.
    [[nodiscard]] LegacyMigrationReport migrate_legacy_files() const;

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

    // Atomically merges one completed parallel batch into the manifest with a
    // single read/write cycle.
    void mark_sessions_complete(
        const CompletedMarketSessions& sessions
    ) const;

    [[nodiscard]] const std::filesystem::path& directory() const noexcept;

private:
    [[nodiscard]] static int market_year(std::int64_t epoch_seconds);
    [[nodiscard]] std::filesystem::path path_for(const std::string& symbol) const;
    [[nodiscard]] std::filesystem::path partition_path_for(
        const std::string& symbol,
        int year
    ) const;
    [[nodiscard]] std::vector<int> partition_years_for(
        const std::string& symbol
    ) const;
    [[nodiscard]] std::vector<domain::InstrumentBars> load_impl(
        std::span<const std::string> symbols,
        std::optional<std::int64_t> first_epoch_seconds
    ) const;
    [[nodiscard]] domain::InstrumentBars load_partition(
        const std::string& symbol,
        int year
    ) const;
    void migrate_legacy_partitions(
        const std::string& symbol,
        bool force_refresh = false
    ) const;
    void write_partition(
        const std::string& symbol,
        int year,
        std::span<const domain::MarketBar> bars
    ) const;
    [[nodiscard]] std::filesystem::path completed_sessions_path() const;

    std::filesystem::path directory_;
};

} // namespace daytrader::storage
