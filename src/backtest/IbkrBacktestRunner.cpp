#include "daytrader/backtest/IbkrBacktestRunner.hpp"

#include "daytrader/backtest/DayTradeBacktester.hpp"
#include "daytrader/ibkr/TwsMarketDataClient.hpp"
#include "daytrader/market_data/InstrumentBarsMerger.hpp"
#include "daytrader/storage/MarketDataCsvStore.hpp"
#include "daytrader/universe/EtfDefinition.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace daytrader::backtest {
namespace {

constexpr int batch_calendar_days = 30;
constexpr int recent_refresh_days = 5;
constexpr auto cache_refresh_age = std::chrono::hours{6};

[[nodiscard]] const std::vector<std::string>& cached_symbols()
{
    static const std::vector<std::string> symbols{
        "SPY",
        "QQQ",
        "TQQQ",
        "SOXX",
        "SOXL",
    };
    return symbols;
}

[[nodiscard]] const universe::EtfDefinition& find_etf(
    const config::AppConfig& config,
    const std::string& symbol
)
{
    const auto found = std::ranges::find(
        config.etfs,
        symbol,
        [](const universe::EtfDefinition& etf) { return etf.market_data.symbol; }
    );
    if (found == config.etfs.end()) {
        throw std::runtime_error("ETF universe is missing " + symbol);
    }
    return *found;
}

[[nodiscard]] config::HistoricalDataSettings request_for(
    const config::AppConfig& config,
    const std::string& symbol,
    int duration_days,
    int end_delay_days
)
{
    config::HistoricalDataSettings request;
    if (symbol == "SOXL" || symbol == "TQQQ") {
        const bool is_tqqq = symbol == "TQQQ";
        request = find_etf(config, is_tqqq ? "QQQ" : "SOXX").market_data;
        request.symbol = symbol;
        request.primary_exchange = is_tqqq ? "NASDAQ" : "ARCA";
    } else {
        request = find_etf(config, symbol).market_data;
    }
    request.duration = std::to_string(duration_days) + " D";
    request.bar_size = "5 mins";
    request.data_type = "TRADES";
    request.regular_trading_hours_only = true;
    request.end_delay = std::chrono::minutes{end_delay_days * 24 * 60};
    request.required = true;
    request.maximum_bars = 4'096;
    return request;
}

[[nodiscard]] std::vector<domain::InstrumentBars> fetch_history(
    const config::AppConfig& config,
    int calendar_days,
    std::span<const std::string> request_symbols,
    std::vector<domain::InstrumentBars> history,
    const storage::MarketDataCsvStore& store
)
{
    const int oldest_batch_delay = ((calendar_days - 1) / batch_calendar_days)
        * batch_calendar_days;
    for (int end_delay_days = oldest_batch_delay; end_delay_days >= 0;
         end_delay_days -= batch_calendar_days) {
        const int duration_days = std::min(
            batch_calendar_days,
            calendar_days - end_delay_days
        );
        std::vector<config::HistoricalDataSettings> requests;
        requests.reserve(request_symbols.size());
        for (const auto& symbol : request_symbols) {
            requests.push_back(request_for(
                config,
                symbol,
                duration_days,
                end_delay_days
            ));
        }

        std::clog << "Fetching IBKR RTH batch: " << duration_days
                  << " days ending " << end_delay_days << " days ago\n";
        auto connection = config.ibkr;
        connection.request_timeout = std::max(
            connection.request_timeout,
            std::chrono::seconds{90}
        );
        ibkr::TwsMarketDataClient client{std::move(connection)};
        market_data::merge_instrument_bars(
            history,
            client.fetch_historical_bars(requests)
        );
        market_data::sort_and_deduplicate_bars(history);
        // Persist after every successful batch so an IBKR timeout never loses
        // all previously downloaded history.
        store.save(history);
    }
    return history;
}

[[nodiscard]] int newest_age_days(
    const domain::InstrumentBars& instrument
)
{
    if (instrument.bars.empty()) {
        return std::numeric_limits<int>::max();
    }
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    return static_cast<int>(
        std::max<std::int64_t>(0, now - instrument.bars.back().epoch_seconds)
        / (24 * 60 * 60)
    );
}

[[nodiscard]] std::vector<domain::InstrumentBars> refresh_recent_history(
    const config::AppConfig& config,
    int duration_days,
    std::span<const std::string> request_symbols,
    std::vector<domain::InstrumentBars> history,
    const storage::MarketDataCsvStore& store
)
{
    std::vector<config::HistoricalDataSettings> requests;
    requests.reserve(request_symbols.size());
    for (const auto& symbol : request_symbols) {
        requests.push_back(request_for(config, symbol, duration_days, 0));
    }
    std::clog << "Refreshing local IBKR cache with the latest " << duration_days
              << " RTH days\n";
    auto connection = config.ibkr;
    connection.request_timeout = std::max(
        connection.request_timeout,
        std::chrono::seconds{90}
    );
    ibkr::TwsMarketDataClient client{std::move(connection)};
    market_data::merge_instrument_bars(
        history,
        client.fetch_historical_bars(requests)
    );
    market_data::sort_and_deduplicate_bars(history);
    store.save(history);
    return history;
}

[[nodiscard]] std::vector<domain::InstrumentBars> load_or_fetch_history(
    const config::AppConfig& config,
    int calendar_days
)
{
    const storage::MarketDataCsvStore store{config.data_directory};
    auto history = store.load(cached_symbols());
    market_data::sort_and_deduplicate_bars(history);
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto target_start = std::chrono::duration_cast<std::chrono::seconds>(
        now - std::chrono::hours{calendar_days * 24}
    ).count();
    std::vector<std::string> full_download_symbols;
    std::vector<std::string> refresh_symbols;
    int refresh_duration_days = recent_refresh_days;

    for (const auto& symbol : cached_symbols()) {
        const auto instrument = std::ranges::find(
            history,
            symbol,
            &domain::InstrumentBars::symbol
        );
        const bool has_historical_start = instrument != history.end()
            && !instrument->bars.empty()
            && instrument->bars.front().epoch_seconds <= target_start;
        const int age_days = instrument == history.end()
            ? std::numeric_limits<int>::max()
            : newest_age_days(*instrument);
        if (!has_historical_start || age_days > batch_calendar_days) {
            full_download_symbols.push_back(symbol);
            continue;
        }

        const std::span<const std::string> one_symbol{&symbol, 1};
        if (age_days > recent_refresh_days
            || !store.is_recent(one_symbol, cache_refresh_age)) {
            refresh_symbols.push_back(symbol);
            refresh_duration_days = std::max(
                refresh_duration_days,
                std::clamp(age_days + 2, recent_refresh_days, batch_calendar_days)
            );
        }
    }

    if (full_download_symbols.empty() && refresh_symbols.empty()) {
        std::clog << "Using local IBKR cache: " << store.directory().string() << '\n';
        return history;
    }

    if (!full_download_symbols.empty()) {
        std::clog << "Local cache needs full history for:";
        for (const auto& symbol : full_download_symbols) {
            std::clog << ' ' << symbol;
        }
        std::clog << "\nDownloading into " << store.directory().string() << '\n';
        history = fetch_history(
            config,
            calendar_days,
            full_download_symbols,
            std::move(history),
            store
        );
    }
    if (!refresh_symbols.empty()) {
        try {
            history = refresh_recent_history(
                config,
                refresh_duration_days,
                refresh_symbols,
                history,
                store
            );
        } catch (const std::exception& exception) {
            // A stale-but-complete cache is still useful for deterministic
            // research when TWS is closed. Missing historical coverage remains
            // fatal in the full-download path above.
            std::clog << "IBKR recent refresh unavailable; using existing cache: "
                      << exception.what() << '\n';
        }
    }
    return history;
}

} // namespace

std::vector<BacktestReport> IbkrBacktestRunner::run(
    const config::AppConfig& config,
    int calendar_days
) const
{
    if (calendar_days <= 0 || calendar_days > 730) {
        throw std::invalid_argument("backtest calendar days must be between 1 and 730");
    }

    const auto history = load_or_fetch_history(config, calendar_days);
    const auto cutoff = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
        - std::chrono::hours{calendar_days * 24}
    ).count();
    std::vector<BacktestReport> reports;
    reports.reserve(2);
    reports.push_back(DayTradeBacktester{DayTradeBacktestSettings{
        .strategy_name = "SOXX VWAP",
        .time_zone = config.time_zone,
        .require_leveraged_vwap_zone = false,
        .earliest_entry_timestamp = cutoff,
    }}.run(history));
    reports.push_back(DayTradeBacktester{DayTradeBacktestSettings{
        .strategy_name = "SOXX + SOXL VWAP",
        .time_zone = config.time_zone,
        .require_leveraged_vwap_zone = true,
        .earliest_entry_timestamp = cutoff,
    }}.run(history));
    return reports;
}

} // namespace daytrader::backtest
