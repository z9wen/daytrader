#include "daytrader/universe/EtfUniverse.hpp"

#include <string>
#include <unordered_set>
#include <utility>

namespace daytrader::universe {
namespace {

[[nodiscard]] EtfDefinition make_etf(
    std::string symbol,
    std::string name,
    EtfGroup group,
    std::string benchmark_symbol,
    std::string leveraged_long_symbol = {},
    std::string leveraged_short_symbol = {},
    std::string primary_exchange = "ARCA"
)
{
    return EtfDefinition{
        .market_data = config::HistoricalDataSettings{
            .symbol = std::move(symbol),
            .primary_exchange = std::move(primary_exchange),
        },
        .name = std::move(name),
        .group = group,
        .benchmark_symbol = std::move(benchmark_symbol),
        .leveraged_long_symbol = std::move(leveraged_long_symbol),
        .leveraged_short_symbol = std::move(leveraged_short_symbol),
    };
}

[[nodiscard]] std::string leveraged_primary_exchange(const std::string& symbol)
{
    return symbol == "TQQQ" || symbol == "SQQQ" ? "NASDAQ" : "ARCA";
}

void append_leveraged_request(
    std::vector<config::HistoricalDataSettings>& requests,
    std::unordered_set<std::string>& symbols,
    const config::HistoricalDataSettings& signal_settings,
    const std::string& leveraged_symbol
)
{
    if (leveraged_symbol.empty() || !symbols.insert(leveraged_symbol).second) {
        return;
    }

    auto request = signal_settings;
    request.symbol = leveraged_symbol;
    request.primary_exchange = leveraged_primary_exchange(leveraged_symbol);
    requests.push_back(std::move(request));
}

} // namespace

std::vector<EtfDefinition> default_etf_universe()
{
    using enum EtfGroup;
    return {
        make_etf("SPY", "S&P 500", broad_market, {}, "SPXL", "SPXS"),
        make_etf("QQQ", "Nasdaq-100", broad_market, "SPY", "TQQQ", "SQQQ", "NASDAQ"),

        make_etf("XLC", "Communication Services", sector, "SPY"),
        make_etf("XLY", "Consumer Discretionary", sector, "SPY"),
        make_etf("XLP", "Consumer Staples", sector, "SPY"),
        make_etf("XLE", "Energy", sector, "SPY"),
        make_etf("XLF", "Financials", sector, "SPY"),
        make_etf("XLV", "Health Care", sector, "SPY"),
        make_etf("XLI", "Industrials", sector, "SPY"),
        make_etf("XLB", "Materials", sector, "SPY"),
        make_etf("XLRE", "Real Estate", sector, "SPY"),
        make_etf("XLK", "Technology", sector, "SPY", "TECL", "TECS"),
        make_etf("XLU", "Utilities", sector, "SPY"),

        make_etf("SOXX", "Semiconductors", industry, "SPY", "SOXL", "SOXS", "NASDAQ"),
        make_etf("XSD", "Equal-Weight Semiconductors", industry, "SPY"),
        make_etf("XSW", "Software", industry, "SPY"),
        make_etf("FDN", "Internet", industry, "SPY", "WEBL", "WEBS"),
        make_etf("XTL", "Telecom", industry, "SPY"),
        make_etf("XBI", "Biotechnology", industry, "SPY", "LABU", "LABD"),
        make_etf("XPH", "Pharmaceuticals", industry, "SPY", "PILL"),
        make_etf("XHE", "Health Care Equipment", industry, "SPY"),
        make_etf("XHS", "Health Care Services", industry, "SPY"),
        make_etf("KBE", "Banks", industry, "SPY"),
        make_etf("KRE", "Regional Banks", industry, "SPY", "DPST"),
        make_etf("KCE", "Capital Markets", industry, "SPY"),
        make_etf("KIE", "Insurance", industry, "SPY"),
        make_etf("XRT", "Retail", industry, "SPY", "RETL"),
        make_etf("XHB", "Homebuilders", industry, "SPY"),
        make_etf("XOP", "Oil & Gas Exploration", industry, "SPY", "GUSH", "DRIP"),
        make_etf("XES", "Oil & Gas Equipment", industry, "SPY"),
        make_etf("XME", "Metals & Mining", industry, "SPY"),
        make_etf("XAR", "Aerospace & Defense", industry, "SPY"),
        make_etf("XTN", "Transportation", industry, "SPY"),
    };
}

std::vector<config::HistoricalDataSettings> historical_data_requests(
    std::span<const EtfDefinition> etfs
)
{
    std::vector<config::HistoricalDataSettings> requests;
    requests.reserve(etfs.size());
    for (const auto& etf : etfs) {
        requests.push_back(etf.market_data);
    }
    return requests;
}

std::vector<config::HistoricalDataSettings> monitoring_data_requests(
    std::span<const EtfDefinition> etfs
)
{
    auto requests = historical_data_requests(etfs);
    if (!etfs.empty()) {
        auto vix = etfs.front().market_data;
        vix.symbol = "VIX";
        vix.security_type = "IND";
        vix.exchange = "CBOE";
        vix.primary_exchange.clear();
        vix.data_type = "TRADES";
        vix.required = false;
        requests.push_back(std::move(vix));
    }

    std::unordered_set<std::string> symbols;
    symbols.reserve(requests.size() * 2);
    for (const auto& request : requests) {
        symbols.insert(request.symbol);
    }

    for (const auto& etf : etfs) {
        if (etf.group == EtfGroup::broad_market && etf.market_data.symbol != "QQQ") {
            continue;
        }
        append_leveraged_request(
            requests,
            symbols,
            etf.market_data,
            etf.leveraged_long_symbol
        );
    }
    return requests;
}

std::vector<std::string> signal_symbols(std::span<const EtfDefinition> etfs)
{
    std::vector<std::string> symbols;
    symbols.reserve(etfs.size());
    for (const auto& etf : etfs) {
        symbols.push_back(etf.market_data.symbol);
    }
    return symbols;
}

} // namespace daytrader::universe
