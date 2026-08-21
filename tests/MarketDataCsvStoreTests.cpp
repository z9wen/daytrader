#include "daytrader/storage/MarketDataCsvStore.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
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

void csv_round_trip_preserves_optional_ibkr_fields()
{
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path()
        / ("daytrader-market-data-store-" + std::to_string(unique));
    const daytrader::storage::MarketDataCsvStore store{directory};
    const std::vector<daytrader::domain::InstrumentBars> original{
        daytrader::domain::InstrumentBars{
            .symbol = "SOXX",
            .bars = {
                daytrader::domain::MarketBar{
                    .epoch_seconds = 1'700'000'000,
                    .open = 100.1,
                    .high = 101.2,
                    .low = 99.8,
                    .close = 100.9,
                    .volume = 123'456.0,
                    .weighted_average_price = 100.75,
                    .trade_count = 789,
                },
                daytrader::domain::MarketBar{
                    .epoch_seconds = 1'700'000'300,
                    .open = 100.9,
                    .high = 101.0,
                    .low = 100.2,
                    .close = 100.4,
                },
                daytrader::domain::MarketBar{
                    .epoch_seconds = 1'704'205'800,
                    .open = 102.0,
                    .high = 103.0,
                    .low = 101.0,
                    .close = 102.5,
                    .volume = 10'000.0,
                },
            },
        },
    };

    store.save(original);
    require(
        std::filesystem::exists(directory / "SOXX" / "2023.csv"),
        "2023 bars should be stored in their year partition"
    );
    require(
        std::filesystem::exists(directory / "SOXX" / "2024.csv"),
        "2024 bars should be stored in their year partition"
    );
    const std::vector<std::string> symbols{"SOXX", "SPY"};
    const auto loaded = store.load(symbols);
    require(loaded.size() == 2, "load should return every requested symbol");
    require(loaded[0].bars.size() == 3, "SOXX cache should combine year partitions");
    require(loaded[1].bars.empty(), "missing SPY file should produce an empty series");
    require(loaded[0].bars[0].trade_count == 789, "trade count should round-trip");
    require(
        std::abs(*loaded[0].bars[0].weighted_average_price - 100.75) < 1e-12,
        "WAP should round-trip"
    );
    require(!loaded[0].bars[1].volume.has_value(), "missing volume should stay empty");
    const auto recent = store.load_since(symbols, 1'704'205'800);
    require(
        recent[0].bars.size() == 1
            && recent[0].bars[0].epoch_seconds == 1'704'205'800,
        "bounded loading should skip older year partitions"
    );
    require(
        store.is_recent(std::vector<std::string>{"SOXX"}, std::chrono::hours{1}),
        "newly written cache should be recent"
    );

    const std::vector<daytrader::domain::InstrumentBars> refresh{
        daytrader::domain::InstrumentBars{
            .symbol = "SOXX",
            .bars = {
                daytrader::domain::MarketBar{
                    .epoch_seconds = 1'704'205'800,
                    .open = 102.0,
                    .high = 104.0,
                    .low = 101.0,
                    .close = 103.5,
                },
                daytrader::domain::MarketBar{
                    .epoch_seconds = 1'704'205'860,
                    .open = 103.5,
                    .high = 104.0,
                    .low = 103.0,
                    .close = 103.8,
                },
            },
        },
    };
    store.merge(refresh);
    const auto refreshed = store.load(std::vector<std::string>{"SOXX"});
    require(refreshed[0].bars.size() == 4, "incremental merge should add one bar");
    require(
        std::abs(refreshed[0].bars[2].close - 103.5) < 1e-12,
        "incoming duplicate timestamp should refresh the cached bar"
    );

    store.mark_session_complete("SOXX", "2026-08-18");
    store.mark_session_complete("SOXX", "2026-08-18");
    store.mark_session_complete("QQQ", "2026-08-18");
    store.mark_sessions_complete(daytrader::storage::CompletedMarketSessions{
        {"SOXX", {"2026-08-17", "2026-08-18"}},
        {"QQQ", {"2026-08-17"}},
    });
    const auto completed = store.load_completed_sessions();
    require(completed.at("SOXX").size() == 2, "completion rows should be idempotent");
    require(completed.at("SOXX").contains("2026-08-18"), "SOXX day should be complete");
    require(completed.at("QQQ").contains("2026-08-18"), "QQQ day should be complete");
    require(completed.at("SOXX").contains("2026-08-17"), "batch SOXX day should persist");
    require(completed.at("QQQ").contains("2026-08-17"), "batch QQQ day should persist");

    std::filesystem::remove_all(directory);
}

void legacy_symbol_file_is_migrated_without_deletion()
{
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path()
        / ("daytrader-market-data-legacy-" + std::to_string(unique));
    std::filesystem::create_directories(directory);
    const auto legacy = directory / "QQQ.csv";
    {
        std::ofstream output{legacy};
        output << "epoch_seconds,open,high,low,close,volume,weighted_average_price,trade_count\n"
               << "1700000000,100,101,99,100.5,1000,100.4,10\n";
    }

    const daytrader::storage::MarketDataCsvStore store{directory};
    const std::vector<daytrader::domain::InstrumentBars> incoming{
        daytrader::domain::InstrumentBars{
            .symbol = "QQQ",
            .bars = {
                daytrader::domain::MarketBar{
                    .epoch_seconds = 1'704'205'800,
                    .open = 102.0,
                    .high = 103.0,
                    .low = 101.0,
                    .close = 102.5,
                },
            },
        },
    };
    store.merge(incoming);

    require(std::filesystem::exists(legacy), "legacy cache must remain recoverable");
    require(
        std::filesystem::exists(directory / "QQQ" / "2023.csv"),
        "legacy year should be copied into a partition"
    );
    require(
        std::filesystem::exists(directory / "QQQ" / "2024.csv"),
        "incoming year should have its own partition"
    );
    require(
        std::filesystem::exists(directory / "QQQ" / ".legacy_imported"),
        "completed migration should be durable"
    );
    const auto loaded = store.load(std::vector<std::string>{"QQQ"});
    require(loaded[0].bars.size() == 2, "legacy and partition bars should load once");

    std::filesystem::remove_all(directory);
}

void explicit_legacy_migration_archives_verified_sources()
{
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path()
        / ("daytrader-market-data-explicit-migration-" + std::to_string(unique));
    std::filesystem::create_directories(directory);
    {
        std::ofstream output{directory / "QQQ.csv"};
        output << "epoch_seconds,open,high,low,close,volume,weighted_average_price,trade_count\n"
               << "1700000000,100,101,99,100.5,1000,100.4,10\n"
               << "1704205800,102,103,101,102.5,2000,102.4,20\n";
    }
    {
        std::ofstream manifest{directory / ".completed_sessions.csv"};
        manifest << "symbol,market_date\nQQQ,2026-08-18\n";
    }

    const daytrader::storage::MarketDataCsvStore store{directory};
    const auto report = store.migrate_legacy_files();
    require(report.symbols == 1, "explicit migration should process one symbol");
    require(report.bars == 2, "explicit migration should report source rows");
    require(report.year_partitions == 2, "bars should be split across two years");
    require(
        !std::filesystem::exists(directory / "QQQ.csv"),
        "migrated root CSV should no longer clutter the cache root"
    );
    require(
        std::filesystem::exists(directory / "legacy_flat" / "QQQ.csv"),
        "legacy source should remain available as a backup"
    );
    require(
        std::filesystem::exists(directory / ".completed_sessions.csv"),
        "completion manifest must remain in the cache root"
    );
    const auto loaded = store.load(std::vector<std::string>{"QQQ"});
    require(loaded[0].bars.size() == 2, "partitioned cache should preserve all rows");

    const auto repeated = store.migrate_legacy_files();
    require(repeated.symbols == 0, "repeated migration should be a no-op");

    std::filesystem::remove_all(directory);
}

} // namespace

int main()
{
    try {
        csv_round_trip_preserves_optional_ibkr_fields();
        legacy_symbol_file_is_migrated_without_deletion();
        explicit_legacy_migration_archives_verified_sources();
        std::cout << "MarketDataCsvStoreTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "MarketDataCsvStoreTests failed: " << exception.what() << '\n';
        return 1;
    }
}
