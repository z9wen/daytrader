#include "daytrader/market_data/InstrumentBarsMerger.hpp"

#include <algorithm>
#include <iterator>
#include <ranges>
#include <utility>

namespace daytrader::market_data {

void merge_instrument_bars(
    std::vector<domain::InstrumentBars>& destination,
    std::span<const domain::InstrumentBars> incoming
)
{
    for (const auto& instrument : incoming) {
        auto existing = std::ranges::find(
            destination,
            instrument.symbol,
            &domain::InstrumentBars::symbol
        );
        if (existing == destination.end()) {
            destination.push_back(instrument);
            continue;
        }
        existing->bars.insert(
            existing->bars.end(),
            instrument.bars.begin(),
            instrument.bars.end()
        );
    }
}

void merge_instrument_bars(
    std::vector<domain::InstrumentBars>& destination,
    std::vector<domain::InstrumentBars>&& incoming
)
{
    for (auto& instrument : incoming) {
        auto existing = std::ranges::find(
            destination,
            instrument.symbol,
            &domain::InstrumentBars::symbol
        );
        if (existing == destination.end()) {
            destination.push_back(std::move(instrument));
            continue;
        }
        existing->bars.insert(
            existing->bars.end(),
            std::make_move_iterator(instrument.bars.begin()),
            std::make_move_iterator(instrument.bars.end())
        );
    }
}

void sort_and_deduplicate_bars(
    std::vector<domain::InstrumentBars>& instruments
)
{
    for (auto& instrument : instruments) {
        sort_and_deduplicate_bars(instrument);
    }
}

void sort_and_deduplicate_bars(domain::InstrumentBars& instrument)
{
    std::ranges::sort(instrument.bars, {}, &domain::MarketBar::epoch_seconds);
    const auto duplicate = std::ranges::unique(
        instrument.bars,
        {},
        &domain::MarketBar::epoch_seconds
    );
    instrument.bars.erase(duplicate.begin(), duplicate.end());
}

} // namespace daytrader::market_data
