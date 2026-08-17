#include "daytrader/universe/EtfUniverse.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void default_universe_is_complete_and_self_contained()
{
    using daytrader::universe::EtfGroup;
    const auto etfs = daytrader::universe::default_etf_universe();
    std::size_t market_count{};
    std::size_t sector_count{};
    std::size_t industry_count{};
    std::unordered_set<std::string> symbols;

    for (const auto& item : etfs) {
        require(!item.market_data.symbol.empty(), "ETF symbol cannot be empty");
        require(symbols.insert(item.market_data.symbol).second, "ETF symbols must be unique");
        switch (item.group) {
        case EtfGroup::broad_market:
            ++market_count;
            break;
        case EtfGroup::sector:
            ++sector_count;
            break;
        case EtfGroup::industry:
            ++industry_count;
            break;
        }
    }

    for (const auto& item : etfs) {
        require(
            item.benchmark_symbol.empty() || symbols.contains(item.benchmark_symbol),
            item.market_data.symbol + " benchmark must exist in the universe"
        );
    }

    require(etfs.size() == 33, "expected 33 ETFs in the default universe");
    require(market_count == 2, "expected two broad-market ETFs");
    require(sector_count == 11, "expected eleven standard-sector ETFs");
    require(industry_count == 20, "expected twenty industry ETFs");
    constexpr std::array sector_symbols{
        "XLC", "XLY", "XLP", "XLE", "XLF", "XLV",
        "XLI", "XLB", "XLRE", "XLK", "XLU",
    };
    for (const auto* symbol : sector_symbols) {
        require(symbols.contains(symbol), std::string{"missing sector ETF "} + symbol);
    }
    require(
        daytrader::universe::historical_data_requests(etfs).size() == etfs.size(),
        "expected one request per ETF"
    );

    const auto monitoring = daytrader::universe::monitoring_data_requests(etfs);
    require(monitoring.size() == 43, "expected signals, VIX, and long-leveraged requests");
    std::unordered_set<std::string> monitoring_symbols;
    for (const auto& request : monitoring) {
        require(
            monitoring_symbols.insert(request.symbol).second,
            "monitoring requests must be unique"
        );
        if (request.symbol == "TQQQ" || request.symbol == "SQQQ") {
            require(
                request.primary_exchange == "NASDAQ",
                request.symbol + " should use NASDAQ as its primary exchange"
            );
        }
        if (request.symbol == "VIX") {
            require(request.security_type == "IND", "VIX should use an index contract");
            require(request.exchange == "CBOE", "VIX should use the CBOE exchange");
            require(!request.required, "VIX should not block the core ETF monitor");
        }
    }
    require(monitoring_symbols.contains("VIX"), "expected optional VIX market data");
    require(monitoring_symbols.contains("SOXL"), "expected SOXL market data");
    require(monitoring_symbols.contains("TECL"), "expected TECL market data");
    require(monitoring_symbols.contains("TQQQ"), "expected TQQQ market data");
    require(!monitoring_symbols.contains("SOXS"), "SOXS must remain reference-only");
    require(!monitoring_symbols.contains("TECS"), "TECS must remain reference-only");
    require(!monitoring_symbols.contains("SQQQ"), "SQQQ must remain reference-only");
    require(!monitoring_symbols.contains("DRIP"), "DRIP must remain reference-only");
    require(!monitoring_symbols.contains("SPXL"), "SPXL is not used by a displayed entry zone");
}

} // namespace

int main()
{
    try {
        default_universe_is_complete_and_self_contained();
        std::cout << "EtfUniverseTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "EtfUniverseTests failed: " << exception.what() << '\n';
        return 1;
    }
}
