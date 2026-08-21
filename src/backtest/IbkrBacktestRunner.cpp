#include "daytrader/backtest/IbkrBacktestRunner.hpp"

#include "daytrader/backtest/DayTradeBacktester.hpp"
#include "daytrader/ibkr/IbkrErrorClassifier.hpp"
#include "daytrader/ibkr/HistoricalRequestPlanner.hpp"
#include "daytrader/ibkr/TwsMarketDataClient.hpp"
#include "daytrader/market_data/InstrumentBarsMerger.hpp"
#include "daytrader/storage/MarketDataCsvStore.hpp"
#include "daytrader/storage/TradingScheduleCsvStore.hpp"
#include "daytrader/time/TimeZoneFormatter.hpp"
#include "daytrader/universe/EtfUniverse.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace daytrader::backtest {
namespace {

constexpr int recent_refresh_days = 5;
// Each request remains one symbol-day so every response is independently
// cacheable. The official API permits up to 50 open historical requests; send
// them asynchronously on one connection and shrink only after an actual IBKR
// pacing/rate response.
constexpr int one_minute_request_days = 1;
constexpr std::size_t maximum_parallel_historical_requests = 50;
constexpr auto cache_refresh_age = std::chrono::hours{6};
constexpr std::string_view schedule_reference_symbol = "SPY";

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

[[nodiscard]] config::HistoricalDataSettings find_market_data_request(
    const config::AppConfig& config,
    const std::string& symbol
)
{
    const auto requests = universe::cacheable_etf_requests(config.etfs);
    const auto found = std::ranges::find(
        requests,
        symbol,
        &config::HistoricalDataSettings::symbol
    );
    if (found == requests.end()) {
        throw std::runtime_error("monitoring ETF universe is missing " + symbol);
    }
    return *found;
}

[[nodiscard]] config::HistoricalDataSettings request_for(
    const config::AppConfig& config,
    const std::string& symbol,
    int duration_days,
    int end_delay_days,
    std::optional<std::int64_t> end_timestamp = std::nullopt
)
{
    auto request = find_market_data_request(config, symbol);
    request.duration = std::to_string(duration_days) + " D";
    request.bar_size = "1 min";
    request.data_type = "TRADES";
    request.regular_trading_hours_only = false;
    request.end_timestamp = end_timestamp;
    if (!end_timestamp.has_value()) {
        request.end_delay = std::chrono::hours{
            static_cast<std::int64_t>(end_delay_days) * 24
        };
    }
    request.required = true;
    return request;
}

[[nodiscard]] std::chrono::sys_days parse_market_day(const std::string& date)
{
    const int year = std::stoi(date.substr(0, 4));
    const unsigned month = static_cast<unsigned>(std::stoi(date.substr(5, 2)));
    const unsigned day = static_cast<unsigned>(std::stoi(date.substr(8, 2)));
    return std::chrono::sys_days{std::chrono::year_month_day{
        std::chrono::year{year},
        std::chrono::month{month},
        std::chrono::day{day},
    }};
}

[[nodiscard]] std::chrono::sys_days current_market_day(
    const std::string& time_zone
)
{
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    return parse_market_day(
        time::TimeZoneFormatter{time_zone}.format_date(now)
    );
}

[[nodiscard]] std::chrono::sys_days latest_complete_extended_market_day(
    const std::string& time_zone
)
{
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    const time::TimeZoneFormatter formatter{time_zone};
    const auto today = parse_market_day(formatter.format_date(now));
    // ETF extended trading ends at 20:00 New York. Before then, today's
    // response is necessarily partial and must not enter the durable manifest.
    return formatter.minutes_since_midnight(now) >= 20 * 60
        ? today
        : today - std::chrono::days{1};
}

[[nodiscard]] std::int64_t daily_request_end(
    std::chrono::sys_days market_day
)
{
    // 06:00 UTC on the following day is 01:00 EST / 02:00 EDT: after the
    // 20:00 New York extended close and before the next 04:00 premarket open.
    const std::chrono::sys_seconds end{
        market_day + std::chrono::days{1}
    };
    return (end + std::chrono::hours{6}).time_since_epoch().count();
}

[[nodiscard]] std::string market_day_string(std::chrono::sys_days day)
{
    const std::chrono::year_month_day date{day};
    std::string result = std::to_string(static_cast<int>(date.year())) + '-';
    const auto append_two = [&result](unsigned value) {
        if (value < 10) {
            result.push_back('0');
        }
        result += std::to_string(value);
    };
    append_two(static_cast<unsigned>(date.month()));
    result.push_back('-');
    append_two(static_cast<unsigned>(date.day()));
    return result;
}

[[nodiscard]] config::HistoricalDataSettings schedule_request_for(
    const config::AppConfig& config,
    std::chrono::sys_days end_day
)
{
    auto request = find_market_data_request(
        config,
        std::string{schedule_reference_symbol}
    );
    request.duration = "1 Y";
    request.bar_size = "1 day";
    request.data_type = "SCHEDULE";
    request.regular_trading_hours_only = true;
    request.end_delay = std::chrono::minutes::zero();
    request.end_timestamp = daily_request_end(end_day);
    request.required = true;
    return request;
}

[[nodiscard]] std::unordered_map<std::string, std::unordered_set<std::string>>
complete_extended_sessions(
    std::span<const domain::InstrumentBars> history,
    const std::string& time_zone
)
{
    struct Coverage {
        std::size_t bars{};
        int first_minute{24 * 60};
        int last_minute{};
    };
    std::unordered_map<std::string, std::unordered_set<std::string>> result;
    const time::TimeZoneFormatter formatter{time_zone};
    for (const auto& instrument : history) {
        std::unordered_map<std::string, Coverage> by_date;
        for (const auto& bar : instrument.bars) {
            const auto date = formatter.format_date(bar.epoch_seconds);
            const int minute = formatter.minutes_since_midnight(bar.epoch_seconds);
            auto& coverage = by_date[date];
            ++coverage.bars;
            coverage.first_minute = std::min(coverage.first_minute, minute);
            coverage.last_minute = std::max(coverage.last_minute, minute);
        }
        auto& dates = result[instrument.symbol];
        for (const auto& [date, coverage] : by_date) {
            // QQQ and SOXX normally produce all 960 extended-session minutes.
            // A small tolerance permits isolated no-trade minutes while still
            // rejecting partial captures such as an interrupted half session.
            if (coverage.bars >= 950 && coverage.first_minute <= 4 * 60
                && coverage.last_minute >= 20 * 60 - 1) {
                dates.insert(date);
            }
        }
    }
    return result;
}

[[nodiscard]] std::vector<domain::TradingSchedule>
fetch_schedules_with_reactive_retry(
    const config::AppConfig& config,
    const std::vector<config::HistoricalDataSettings>& requests
)
{
    while (true) {
        try {
            auto connection = config.ibkr;
            ibkr::TwsMarketDataClient client{std::move(connection)};
            return client.fetch_historical_schedules(requests);
        } catch (const std::exception& exception) {
            const bool retryable = ibkr::is_pacing_or_rate_limit_error(exception.what())
                || ibkr::is_connection_interruption_error(exception.what());
            if (!retryable) {
                throw;
            }
            std::cerr << "IBKR trading-schedule request interrupted; retrying the "
                         "same request set after reconnect: "
                      << exception.what() << '\n';
            std::this_thread::sleep_for(config.monitoring.reconnect_delay);
        }
    }
}

[[nodiscard]] std::vector<std::chrono::sys_days> load_cached_market_sessions(
    const config::AppConfig& config,
    std::chrono::sys_days first_day,
    std::chrono::sys_days last_day
)
{
    if (first_day > last_day) {
        return {};
    }
    const storage::TradingScheduleCsvStore schedule_store{
        config.minute_data_directory.parent_path() / "schedules"
    };
    const int first_year = static_cast<int>(
        std::chrono::year_month_day{first_day}.year()
    );
    const int last_year = static_cast<int>(
        std::chrono::year_month_day{last_day}.year()
    );
    for (int year = first_year; year <= last_year; ++year) {
        const auto target_end = std::min(
            last_day,
            std::chrono::sys_days{
                std::chrono::year{year} / std::chrono::December
                    / std::chrono::day{31}
            }
        );
        if (!schedule_store.covers_through(
                std::string{schedule_reference_symbol},
                year,
                market_day_string(target_end)
            )) {
            throw std::runtime_error(
                "local IBKR trading schedule does not cover SPY through "
                + market_day_string(target_end)
            );
        }
    }
    const auto schedule = schedule_store.load_range(
        std::string{schedule_reference_symbol},
        market_day_string(first_day),
        market_day_string(last_day)
    );
    std::vector<std::chrono::sys_days> days;
    days.reserve(schedule.sessions.size());
    for (const auto& session : schedule.sessions) {
        days.push_back(parse_market_day(session.market_date));
    }
    return days;
}

[[nodiscard]] std::vector<std::chrono::sys_days> load_or_fetch_market_sessions(
    const config::AppConfig& config,
    std::chrono::sys_days first_day,
    std::chrono::sys_days last_day
)
{
    if (first_day > last_day) {
        return {};
    }
    const auto directory = config.minute_data_directory.parent_path() / "schedules";
    const storage::TradingScheduleCsvStore store{directory};
    const int first_year = static_cast<int>(
        std::chrono::year_month_day{first_day}.year()
    );
    const int last_year = static_cast<int>(
        std::chrono::year_month_day{last_day}.year()
    );
    std::vector<int> requested_years;
    std::vector<std::string> requested_coverage;
    std::vector<config::HistoricalDataSettings> requests;
    for (int year = last_year; year >= first_year; --year) {
        const auto year_end = std::min(
            last_day,
            std::chrono::sys_days{
                std::chrono::year{year} / std::chrono::December
                    / std::chrono::day{31}
            }
        );
        const auto coverage = market_day_string(year_end);
        if (store.covers_through(
                std::string{schedule_reference_symbol}, year, coverage
            )) {
            continue;
        }
        requested_years.push_back(year);
        requested_coverage.push_back(coverage);
        requests.push_back(schedule_request_for(config, year_end));
    }

    if (!requests.empty()) {
        std::clog << "Fetching " << requests.size()
                  << " IBKR annual trading schedules for SPY\n";
        auto fetched = fetch_schedules_with_reactive_retry(config, requests);
        if (fetched.size() != requested_years.size()) {
            throw std::runtime_error(
                "IBKR trading schedules returned an unexpected result count"
            );
        }
        for (std::size_t index = 0; index < fetched.size(); ++index) {
            const int year = requested_years[index];
            const auto prefix = std::to_string(year) + '-';
            std::vector<domain::TradingSession> sessions;
            for (auto& session : fetched[index].sessions) {
                if (session.market_date.starts_with(prefix)) {
                    sessions.push_back(std::move(session));
                }
            }
            if (sessions.empty()) {
                throw std::runtime_error(
                    "IBKR returned no SPY trading sessions for "
                    + std::to_string(year)
                );
            }
            store.save_year(
                std::string{schedule_reference_symbol},
                year,
                sessions,
                requested_coverage[index]
            );
            std::clog << "Cached " << sessions.size()
                      << " SPY trading sessions for " << year << '\n';
        }
    }

    auto days = load_cached_market_sessions(config, first_day, last_day);
    if (days.empty()) {
        throw std::runtime_error(
            "IBKR trading schedule contains no sessions in the requested range"
        );
    }
    std::ranges::sort(days, std::greater{});
    days.erase(std::unique(days.begin(), days.end()), days.end());
    return days;
}

[[nodiscard]] std::vector<domain::InstrumentBars> fetch_with_reactive_retry(
    const config::AppConfig& config,
    const std::vector<config::HistoricalDataSettings>& requests,
    bool retry_pacing = true
)
{
    while (true) {
        try {
            auto connection = config.ibkr;
            ibkr::TwsMarketDataClient client{std::move(connection)};
            return client.fetch_historical_bars(requests);
        } catch (const std::exception& exception) {
            const bool pacing = ibkr::is_pacing_or_rate_limit_error(exception.what());
            const bool disconnected = ibkr::is_connection_interruption_error(
                exception.what()
            );
            if (!pacing && !disconnected) {
                throw;
            }
            if (pacing && !retry_pacing) {
                throw;
            }
            std::cerr << (pacing
                    ? "IBKR pacing response received"
                    : "IBKR historical-data connection interrupted")
                      << "; reconnecting and retrying the same complete batch: "
                      << exception.what() << '\n';
            std::this_thread::sleep_for(config.monitoring.reconnect_delay);
        }
    }
}

[[nodiscard]] std::vector<domain::InstrumentBars> fetch_history(
    const config::AppConfig& config,
    int calendar_days,
    std::span<const std::string> request_symbols,
    std::vector<domain::InstrumentBars> history,
    const storage::MarketDataCsvStore& store,
    bool retain_history = true
)
{
    const auto today = current_market_day(config.time_zone);
    const auto first_day = today - std::chrono::days{calendar_days - 1};
    const auto last_complete_day = latest_complete_extended_market_day(
        config.time_zone
    );
    std::vector<std::pair<ibkr::HistoricalDayWindow, std::chrono::sys_days>> windows;
    const auto market_days = load_or_fetch_market_sessions(
        config,
        first_day,
        last_complete_day
    );
    windows.reserve(market_days.size());
    for (const auto market_day : market_days) {
        windows.emplace_back(
            ibkr::HistoricalDayWindow{
                .duration_days = one_minute_request_days,
                .end_delay_days = static_cast<int>((today - market_day).count()),
            },
            market_day
        );
    }

    struct DailyRequest {
        ibkr::HistoricalDayWindow window;
        std::chrono::sys_days market_day;
        std::string symbol;
    };
    auto completed = complete_extended_sessions(history, config.time_zone);
    for (auto&& [symbol, dates] : store.load_completed_sessions()) {
        completed[symbol].insert(dates.begin(), dates.end());
    }
    std::vector<DailyRequest> pending;
    pending.reserve(windows.size() * request_symbols.size());
    // Finish the newest year before moving backward. Within each year the
    // caller-supplied symbol order keeps QQQ, SPY, and SOXX ahead of breadth.
    std::size_t year_begin{};
    while (year_begin < windows.size()) {
        const int year = static_cast<int>(
            std::chrono::year_month_day{windows[year_begin].second}.year()
        );
        std::size_t year_end = year_begin + 1;
        while (year_end < windows.size()
               && static_cast<int>(
                   std::chrono::year_month_day{windows[year_end].second}.year()
               ) == year) {
            ++year_end;
        }
        for (const auto& symbol : request_symbols) {
            for (std::size_t index = year_begin; index < year_end; ++index) {
                const auto& [window, market_day] = windows[index];
                const auto date = market_day_string(market_day);
                const auto symbol_dates = completed.find(symbol);
                if (symbol_dates != completed.end()
                    && symbol_dates->second.contains(date)) {
                    continue;
                }
                pending.push_back(DailyRequest{
                    .window = window,
                    .market_day = market_day,
                    .symbol = symbol,
                });
            }
        }
        year_begin = year_end;
    }
    std::clog << "Daily cache already complete for "
              << windows.size() * request_symbols.size() - pending.size()
              << " symbol-days; requesting " << pending.size() << " missing days\n";

    std::size_t completed_requests{};
    std::vector<std::string> missing_sessions;
    std::size_t parallel_requests = maximum_parallel_historical_requests;
    while (completed_requests < pending.size()) {
        const auto batch_size = std::min(
            parallel_requests,
            pending.size() - completed_requests
        );
        const auto batch_begin = pending.begin()
            + static_cast<std::ptrdiff_t>(completed_requests);
        const auto batch_end = batch_begin
            + static_cast<std::ptrdiff_t>(batch_size);
        std::vector<config::HistoricalDataSettings> requests;
        requests.reserve(batch_size);
        for (auto item = batch_begin; item != batch_end; ++item) {
            requests.push_back(request_for(
                config,
                item->symbol,
                item->window.duration_days,
                item->window.end_delay_days,
                daily_request_end(item->market_day)
            ));
        }

        std::clog << "Fetching IBKR parallel 1-minute batch "
                  << completed_requests + 1 << '-'
                  << completed_requests + batch_size << '/' << pending.size()
                  << " | open requests " << batch_size << '\n';
        std::vector<domain::InstrumentBars> fetched;
        try {
            fetched = fetch_with_reactive_retry(config, requests, false);
        } catch (const std::exception& exception) {
            if (!ibkr::is_pacing_or_rate_limit_error(exception.what())) {
                throw;
            }
            if (parallel_requests > 1) {
                parallel_requests = std::max<std::size_t>(1, parallel_requests / 2);
                std::clog << "IBKR pacing response reduced the next retry batch to "
                          << parallel_requests << " open requests\n";
            } else {
                std::clog << "IBKR pacing response at one open request; retrying "
                             "after reconnect delay\n";
                std::this_thread::sleep_for(config.monitoring.reconnect_delay);
            }
            continue;
        }
        if (fetched.size() != batch_size) {
            throw std::runtime_error(
                "IBKR parallel historical batch returned an unexpected result count"
            );
        }

        std::unordered_set<std::string> changed_symbols;
        storage::CompletedMarketSessions completed_batch;
        std::vector<std::size_t> received_counts;
        received_counts.reserve(batch_size);
        for (std::size_t index = 0; index < batch_size; ++index) {
            const auto& item = batch_begin[static_cast<std::ptrdiff_t>(index)];
            if (fetched[index].symbol != item.symbol) {
                throw std::runtime_error(
                    "IBKR parallel historical result order did not match its request"
                );
            }
            received_counts.push_back(fetched[index].bars.size());
            if (!fetched[index].bars.empty()) {
                changed_symbols.insert(item.symbol);
                completed_batch[item.symbol].insert(
                    market_day_string(item.market_day)
                );
            } else {
                missing_sessions.push_back(
                    item.symbol + '@' + market_day_string(item.market_day)
                );
            }
        }
        store.merge(fetched);
        if (retain_history) {
            market_data::merge_instrument_bars(history, std::move(fetched));
            for (const auto& symbol : changed_symbols) {
                const auto changed = std::ranges::find(
                    history,
                    symbol,
                    &domain::InstrumentBars::symbol
                );
                if (changed == history.end()) {
                    throw std::runtime_error(
                        "IBKR daily result did not include requested symbol " + symbol
                    );
                }
                market_data::sort_and_deduplicate_bars(*changed);
            }
        }
        store.mark_sessions_complete(completed_batch);

        for (std::size_t index = 0; index < batch_size; ++index) {
            const auto& item = batch_begin[static_cast<std::ptrdiff_t>(index)];
            if (received_counts[index] == 0) {
                std::cerr << "MISSING_DATA 0 returned bars for " << item.symbol
                          << " on scheduled session "
                          << market_day_string(item.market_day)
                          << "; completion marker withheld\n";
            } else {
                std::clog << "Cached " << received_counts[index]
                          << " returned bars for " << item.symbol << " | "
                          << market_day_string(item.market_day) << '\n';
            }
        }
        completed_requests += batch_size;
    }
    if (!missing_sessions.empty()) {
        std::string summary;
        const std::size_t shown = std::min<std::size_t>(missing_sessions.size(), 10);
        for (std::size_t index = 0; index < shown; ++index) {
            if (!summary.empty()) {
                summary += ", ";
            }
            summary += missing_sessions[index];
        }
        if (missing_sessions.size() > shown) {
            summary += ", ...";
        }
        throw std::runtime_error(
            "IBKR returned no bars for " + std::to_string(missing_sessions.size())
            + " scheduled symbol-days; they remain pending for the next run: "
            + summary
        );
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
              << " complete 1-minute days\n";
    auto fetched = fetch_with_reactive_retry(config, requests);
    store.merge(fetched);
    market_data::merge_instrument_bars(history, std::move(fetched));
    market_data::sort_and_deduplicate_bars(history);
    return history;
}

[[nodiscard]] std::vector<domain::InstrumentBars> load_or_fetch_history(
    const config::AppConfig& config,
    int calendar_days,
    std::span<const std::string> request_symbols
)
{
    const storage::MarketDataCsvStore store{config.minute_data_directory};
    auto history = store.load(request_symbols);
    market_data::sort_and_deduplicate_bars(history);
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto target_start = std::chrono::duration_cast<std::chrono::seconds>(
        now - std::chrono::hours{static_cast<std::int64_t>(calendar_days) * 24}
    ).count();
    std::vector<std::string> full_download_symbols;
    std::vector<std::string> refresh_symbols;
    int refresh_duration_days = recent_refresh_days;

    for (const auto& symbol : request_symbols) {
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
        if (!has_historical_start
            || age_days > ibkr::one_minute_max_duration_days) {
            full_download_symbols.push_back(symbol);
            continue;
        }

        const std::span<const std::string> one_symbol{&symbol, 1};
        if (age_days > recent_refresh_days
            || !store.is_recent(one_symbol, cache_refresh_age)) {
            refresh_symbols.push_back(symbol);
            refresh_duration_days = std::max(
                refresh_duration_days,
                std::clamp(
                    age_days + 2,
                    recent_refresh_days,
                    ibkr::one_minute_max_duration_days
                )
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

[[nodiscard]] std::int64_t backtest_cutoff(int calendar_days)
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
        - std::chrono::hours{static_cast<std::int64_t>(calendar_days) * 24}
    ).count();
}

[[nodiscard]] BacktestReport run_qqq_strategy(
    const config::AppConfig& config,
    int calendar_days,
    const std::vector<domain::InstrumentBars>& history
)
{
    return DayTradeBacktester{DayTradeBacktestSettings{
        .strategy_name = "QQQ -> TQQQ",
        .signal_symbol = "QQQ",
        .trade_symbol = "TQQQ",
        .time_zone = config.time_zone,
        .source_bar_interval = std::chrono::minutes{1},
        .trend_bar_interval = std::chrono::minutes{5},
        .earliest_entry_timestamp = backtest_cutoff(calendar_days),
    }}.run(history);
}

[[nodiscard]] BacktestReport run_qqq_trend_cycle_strategy(
    const config::AppConfig& config,
    int calendar_days,
    const std::vector<domain::InstrumentBars>& history
)
{
    return DayTradeBacktester{DayTradeBacktestSettings{
        .strategy_name = "QQQ BUILDING CYCLE -> TQQQ",
        .signal_symbol = "QQQ",
        .trade_symbol = "TQQQ",
        .time_zone = config.time_zone,
        .source_bar_interval = std::chrono::minutes{1},
        .trend_bar_interval = std::chrono::minutes{5},
        .lifecycle_mode = TradeLifecycleMode::trend_cycle,
        // The cycle strategy exits on trend deterioration. Keep only a wider
        // disaster stop so ordinary one-minute noise does not replace that rule.
        .initial_stop_atr = 3.0,
        .earliest_entry_timestamp = backtest_cutoff(calendar_days),
    }}.run(history);
}

[[nodiscard]] BacktestReport run_soxx_strategy(
    const config::AppConfig& config,
    int calendar_days,
    const std::vector<domain::InstrumentBars>& history
)
{
    return DayTradeBacktester{DayTradeBacktestSettings{
        .strategy_name = "SOXX -> SOXL",
        .signal_symbol = "SOXX",
        .trade_symbol = "SOXL",
        .time_zone = config.time_zone,
        .source_bar_interval = std::chrono::minutes{1},
        .trend_bar_interval = std::chrono::minutes{5},
        .earliest_entry_timestamp = backtest_cutoff(calendar_days),
    }}.run(history);
}

[[nodiscard]] BacktestReport run_soxx_trend_cycle_strategy(
    const config::AppConfig& config,
    int calendar_days,
    const std::vector<domain::InstrumentBars>& history
)
{
    return DayTradeBacktester{DayTradeBacktestSettings{
        .strategy_name = "SOXX BUILDING CYCLE -> SOXL",
        .signal_symbol = "SOXX",
        .trade_symbol = "SOXL",
        .time_zone = config.time_zone,
        .source_bar_interval = std::chrono::minutes{1},
        .trend_bar_interval = std::chrono::minutes{5},
        .lifecycle_mode = TradeLifecycleMode::trend_cycle,
        .initial_stop_atr = 3.0,
        .earliest_entry_timestamp = backtest_cutoff(calendar_days),
    }}.run(history);
}

[[nodiscard]] std::vector<domain::InstrumentBars> load_complete_local_cache(
    const config::AppConfig& config,
    int calendar_days,
    const std::vector<std::string>& symbols,
    std::string_view label
)
{
    const storage::MarketDataCsvStore store{config.minute_data_directory};
    auto history = store.load(symbols);
    market_data::sort_and_deduplicate_bars(history);

    const auto today = current_market_day(config.time_zone);
    const auto first_day = today - std::chrono::days{calendar_days - 1};
    const auto last_complete_day = latest_complete_extended_market_day(
        config.time_zone
    );
    const auto sessions = load_cached_market_sessions(
        config,
        first_day,
        last_complete_day
    );
    if (sessions.empty()) {
        throw std::runtime_error(
            "local " + std::string{label}
                + " backtest cannot find the cached market-session calendar"
        );
    }

    auto completed = complete_extended_sessions(history, config.time_zone);
    for (auto&& [symbol, dates] : store.load_completed_sessions()) {
        completed[symbol].insert(dates.begin(), dates.end());
    }

    std::string missing_summary;
    for (const auto& symbol : symbols) {
        std::size_t missing{};
        const auto found = completed.find(symbol);
        for (const auto session : sessions) {
            if (found == completed.end()
                || !found->second.contains(market_day_string(session))) {
                ++missing;
            }
        }
        if (missing > 0) {
            if (!missing_summary.empty()) {
                missing_summary += ", ";
            }
            missing_summary += symbol + " missing " + std::to_string(missing)
                + '/' + std::to_string(sessions.size()) + " sessions";
        }
    }
    if (!missing_summary.empty()) {
        throw std::runtime_error(
            "local " + std::string{label}
                + " backtest cache is incomplete: " + missing_summary
        );
    }
    return history;
}

} // namespace

void IbkrBacktestRunner::cache_history(
    const config::AppConfig& config,
    int calendar_days,
    std::span<const std::string> symbols
) const
{
    if (calendar_days <= 0) {
        throw std::invalid_argument("historical calendar days must be positive");
    }
    if (symbols.empty()) {
        throw std::invalid_argument("at least one cache symbol is required");
    }

    // An explicit cache command audits every expected symbol/session pair.
    // File age and first/last timestamps are insufficient because a process can
    // be interrupted in the middle of YTD while both range endpoints exist.
    const storage::MarketDataCsvStore store{config.minute_data_directory};
    static_cast<void>(fetch_history(
        config,
        calendar_days,
        symbols,
        {},
        store,
        false
    ));
}

std::vector<BacktestReport> IbkrBacktestRunner::run(
    const config::AppConfig& config,
    int calendar_days
) const
{
    if (calendar_days <= 0) {
        throw std::invalid_argument("backtest calendar days must be positive");
    }

    const auto history = load_or_fetch_history(
        config,
        calendar_days,
        cached_symbols()
    );
    std::vector<BacktestReport> reports;
    reports.reserve(2);
    reports.push_back(run_soxx_strategy(config, calendar_days, history));
    reports.push_back(run_qqq_strategy(config, calendar_days, history));
    return reports;
}

std::vector<BacktestReport> IbkrBacktestRunner::run_cached_qqq(
    const config::AppConfig& config,
    int calendar_days
) const
{
    if (calendar_days <= 0) {
        throw std::invalid_argument("backtest calendar days must be positive");
    }
    const auto history = load_complete_local_cache(
        config,
        calendar_days,
        {"SPY", "QQQ", "TQQQ"},
        "QQQ"
    );
    std::vector<BacktestReport> reports;
    reports.reserve(2);
    reports.push_back(run_qqq_strategy(config, calendar_days, history));
    reports.push_back(run_qqq_trend_cycle_strategy(config, calendar_days, history));
    return reports;
}

std::vector<BacktestReport> IbkrBacktestRunner::run_cached_core(
    const config::AppConfig& config,
    int calendar_days
) const
{
    if (calendar_days <= 0) {
        throw std::invalid_argument("backtest calendar days must be positive");
    }
    const auto history = load_complete_local_cache(
        config,
        calendar_days,
        {"SPY", "QQQ", "TQQQ", "SOXX", "SOXL"},
        "core-five"
    );
    std::vector<BacktestReport> reports;
    reports.reserve(4);
    reports.push_back(run_qqq_strategy(config, calendar_days, history));
    reports.push_back(run_qqq_trend_cycle_strategy(config, calendar_days, history));
    reports.push_back(run_soxx_strategy(config, calendar_days, history));
    reports.push_back(run_soxx_trend_cycle_strategy(config, calendar_days, history));
    return reports;
}

} // namespace daytrader::backtest
