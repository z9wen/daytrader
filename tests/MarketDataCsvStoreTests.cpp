#include "daytrader/storage/MarketDataCsvStore.hpp"

#include <chrono>
#include <cmath>
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
            },
        },
    };

    store.save(original);
    const std::vector<std::string> symbols{"SOXX", "SPY"};
    const auto loaded = store.load(symbols);
    require(loaded.size() == 2, "load should return every requested symbol");
    require(loaded[0].bars.size() == 2, "SOXX cache should contain two bars");
    require(loaded[1].bars.empty(), "missing SPY file should produce an empty series");
    require(loaded[0].bars[0].trade_count == 789, "trade count should round-trip");
    require(
        std::abs(*loaded[0].bars[0].weighted_average_price - 100.75) < 1e-12,
        "WAP should round-trip"
    );
    require(!loaded[0].bars[1].volume.has_value(), "missing volume should stay empty");
    require(
        store.is_recent(std::vector<std::string>{"SOXX"}, std::chrono::hours{1}),
        "newly written cache should be recent"
    );

    std::filesystem::remove_all(directory);
}

} // namespace

int main()
{
    try {
        csv_round_trip_preserves_optional_ibkr_fields();
        std::cout << "MarketDataCsvStoreTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "MarketDataCsvStoreTests failed: " << exception.what() << '\n';
        return 1;
    }
}
