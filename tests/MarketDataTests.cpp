#include "daytrader/market_data/BarSeriesAligner.hpp"
#include "daytrader/market_data/CompletedBarSynchronizer.hpp"
#include "daytrader/market_data/InstrumentBarsLookup.hpp"

#include <chrono>
#include <cstdint>
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

[[nodiscard]] daytrader::domain::MarketBar bar(std::int64_t epoch_seconds)
{
    return daytrader::domain::MarketBar{.epoch_seconds = epoch_seconds, .close = 100.0};
}

void aligner_keeps_only_matching_completed_timestamps()
{
    constexpr std::int64_t start = 1'700'000'000;
    const daytrader::domain::InstrumentBars signal{
        .symbol = "SIGNAL",
        .bars = {bar(start), bar(start + 300), bar(start + 600)},
    };
    const daytrader::domain::InstrumentBars benchmark{
        .symbol = "BENCHMARK",
        .bars = {bar(start), bar(start + 600)},
    };

    const auto aligned = daytrader::market_data::BarSeriesAligner{
        std::chrono::minutes{5}
    }.align_completed(signal, benchmark);
    require(aligned.size() == 2, "expected two matching timestamps");
    require(aligned.front().epoch_seconds == start, "expected chronological output");
    require(aligned.back().epoch_seconds == start + 600, "expected latest common bar");
}

void lookup_rejects_duplicate_symbols()
{
    const std::vector<daytrader::domain::InstrumentBars> duplicate{
        daytrader::domain::InstrumentBars{.symbol = "SPY"},
        daytrader::domain::InstrumentBars{.symbol = "SPY"},
    };

    bool threw{};
    try {
        static_cast<void>(daytrader::market_data::InstrumentBarsLookup{duplicate});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "expected duplicate symbols to be rejected");
}

void completed_bar_synchronizer_waits_for_every_instrument()
{
    constexpr std::int64_t start = 1'700'000'000;
    constexpr std::int64_t now = start + 1'000;
    const std::vector<daytrader::domain::InstrumentBars> instruments{
        daytrader::domain::InstrumentBars{
            .symbol = "SPY",
            .bars = {bar(start), bar(start + 300), bar(start + 600)},
        },
        daytrader::domain::InstrumentBars{
            .symbol = "QQQ",
            .bars = {bar(start), bar(start + 300)},
        },
        daytrader::domain::InstrumentBars{
            .symbol = "SOXX",
            .bars = {bar(start), bar(start + 300), bar(start + 600)},
        },
    };

    const auto completed = daytrader::market_data::latest_common_completed_bar(
        instruments,
        std::chrono::minutes{5},
        now
    );
    require(completed.has_value(), "expected a common completed bar");
    require(*completed == start + 300, "expected the latest timestamp shared by every ETF");
}

} // namespace

int main()
{
    try {
        aligner_keeps_only_matching_completed_timestamps();
        lookup_rejects_duplicate_symbols();
        completed_bar_synchronizer_waits_for_every_instrument();
        std::cout << "MarketDataTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "MarketDataTests failed: " << exception.what() << '\n';
        return 1;
    }
}
