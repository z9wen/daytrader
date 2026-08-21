#include "daytrader/storage/MarketDataCsvStore.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace daytrader::storage {
namespace {

constexpr std::string_view csv_header =
    "epoch_seconds,open,high,low,close,volume,weighted_average_price,trade_count";
constexpr std::string_view completed_sessions_header = "symbol,market_date";

[[nodiscard]] bool valid_market_date(std::string_view value)
{
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 4 || index == 7) {
            continue;
        }
        if (value[index] < '0' || value[index] > '9') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::array<std::string, 8> split_row(
    const std::string& row,
    const std::filesystem::path& path,
    std::size_t line_number
)
{
    std::array<std::string, 8> fields;
    std::size_t begin{};
    for (std::size_t index = 0; index + 1 < fields.size(); ++index) {
        const auto separator = row.find(',', begin);
        if (separator == std::string::npos) {
            throw std::runtime_error(
                "invalid market-data CSV row at " + path.string() + ':'
                + std::to_string(line_number)
            );
        }
        fields[index] = row.substr(begin, separator - begin);
        begin = separator + 1;
    }
    fields.back() = row.substr(begin);
    if (fields.back().find(',') != std::string::npos) {
        throw std::runtime_error(
            "too many market-data CSV fields at " + path.string() + ':'
            + std::to_string(line_number)
        );
    }
    return fields;
}

[[nodiscard]] std::optional<double> optional_double(const std::string& text)
{
    return text.empty() ? std::nullopt : std::optional<double>{std::stod(text)};
}

[[nodiscard]] std::optional<int> optional_integer(const std::string& text)
{
    return text.empty() ? std::nullopt : std::optional<int>{std::stoi(text)};
}

[[nodiscard]] domain::MarketBar parse_bar(
    const std::string& row,
    const std::filesystem::path& path,
    std::size_t line_number
)
{
    const auto fields = split_row(row, path, line_number);
    try {
        return domain::MarketBar{
            .epoch_seconds = std::stoll(fields[0]),
            .open = std::stod(fields[1]),
            .high = std::stod(fields[2]),
            .low = std::stod(fields[3]),
            .close = std::stod(fields[4]),
            .volume = optional_double(fields[5]),
            .weighted_average_price = optional_double(fields[6]),
            .trade_count = optional_integer(fields[7]),
        };
    } catch (const std::exception& exception) {
        throw std::runtime_error(
            "invalid market-data value at " + path.string() + ':'
            + std::to_string(line_number) + ": " + exception.what()
        );
    }
}

void write_optional(std::ostream& output, const std::optional<double>& value)
{
    if (value.has_value()) {
        output << *value;
    }
}

void write_optional(std::ostream& output, const std::optional<int>& value)
{
    if (value.has_value()) {
        output << *value;
    }
}

[[nodiscard]] std::vector<domain::MarketBar> read_bars(
    const std::filesystem::path& path
)
{
    std::ifstream input{path};
    if (!input) {
        throw std::runtime_error("unable to open market-data cache: " + path.string());
    }
    std::string row;
    if (!std::getline(input, row) || row != csv_header) {
        throw std::runtime_error("invalid market-data CSV header: " + path.string());
    }
    std::vector<domain::MarketBar> bars;
    std::size_t line_number = 1;
    while (std::getline(input, row)) {
        ++line_number;
        if (!row.empty()) {
            bars.push_back(parse_bar(row, path, line_number));
        }
    }
    return bars;
}

} // namespace

MarketDataCsvStore::MarketDataCsvStore(std::filesystem::path directory)
    : directory_{std::move(directory)}
{
    if (directory_.empty()) {
        throw std::invalid_argument("market-data cache directory cannot be empty");
    }
}

std::filesystem::path MarketDataCsvStore::path_for(const std::string& symbol) const
{
    if (symbol.empty() || symbol.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")
            != std::string::npos) {
        throw std::invalid_argument("invalid cache symbol: " + symbol);
    }
    return directory_ / (symbol + ".csv");
}

int MarketDataCsvStore::market_year(std::int64_t epoch_seconds)
{
    // New York is always UTC-5 around the calendar-year boundary. Subtracting
    // five hours therefore assigns Dec 31 after-hours bars to the correct
    // market year without retaining a time-zone cache for millions of rows.
    using namespace std::chrono;
    const sys_seconds adjusted{seconds{epoch_seconds} - hours{5}};
    return static_cast<int>(year_month_day{floor<days>(adjusted)}.year());
}

std::filesystem::path MarketDataCsvStore::partition_path_for(
    const std::string& symbol,
    int year
) const
{
    static_cast<void>(path_for(symbol));
    if (year < 1900 || year > 3000) {
        throw std::invalid_argument("invalid cache partition year");
    }
    return directory_ / symbol / (std::to_string(year) + ".csv");
}

std::vector<int> MarketDataCsvStore::partition_years_for(
    const std::string& symbol
) const
{
    static_cast<void>(path_for(symbol));
    const auto symbol_directory = directory_ / symbol;
    std::vector<int> years;
    if (!std::filesystem::is_directory(symbol_directory)) {
        return years;
    }
    for (const auto& entry : std::filesystem::directory_iterator{symbol_directory}) {
        if (!entry.is_regular_file() || entry.path().extension() != ".csv") {
            continue;
        }
        const auto stem = entry.path().stem().string();
        int year{};
        const auto [end, error] = std::from_chars(
            stem.data(), stem.data() + stem.size(), year
        );
        if (error == std::errc{} && end == stem.data() + stem.size()
            && year >= 1900 && year <= 3000) {
            years.push_back(year);
        }
    }
    std::ranges::sort(years);
    years.erase(std::unique(years.begin(), years.end()), years.end());
    return years;
}

domain::InstrumentBars MarketDataCsvStore::load_partition(
    const std::string& symbol,
    int year
) const
{
    domain::InstrumentBars result{.symbol = symbol};
    const auto partition = partition_path_for(symbol, year);
    if (std::filesystem::exists(partition)) {
        result.bars = read_bars(partition);
        return result;
    }

    // Before a symbol is migrated, extract just this year from its legacy file.
    const auto legacy = path_for(symbol);
    if (!std::filesystem::exists(legacy)) {
        return result;
    }
    for (auto& bar : read_bars(legacy)) {
        if (market_year(bar.epoch_seconds) == year) {
            result.bars.push_back(std::move(bar));
        }
    }
    return result;
}

void MarketDataCsvStore::migrate_legacy_partitions(
    const std::string& symbol,
    bool force_refresh
) const
{
    const auto legacy = path_for(symbol);
    const auto marker = directory_ / symbol / ".legacy_imported";
    if (!std::filesystem::exists(legacy)
        || (!force_refresh && std::filesystem::exists(marker))) {
        return;
    }

    std::map<int, std::vector<domain::MarketBar>> partitions;
    for (auto& bar : read_bars(legacy)) {
        partitions[market_year(bar.epoch_seconds)].push_back(std::move(bar));
    }
    for (auto& [year, bars] : partitions) {
        const auto path = partition_path_for(symbol, year);
        if (std::filesystem::exists(path)) {
            auto existing = read_bars(path);
            // A partition may contain a fresher value for a repeated timestamp,
            // so keep it ahead of the legacy copy during stable de-duplication.
            existing.insert(existing.end(), bars.begin(), bars.end());
            bars = std::move(existing);
        }
        std::stable_sort(
            bars.begin(), bars.end(),
            [](const auto& left, const auto& right) {
                return left.epoch_seconds < right.epoch_seconds;
            }
        );
        const auto duplicates = std::ranges::unique(
            bars, {}, &domain::MarketBar::epoch_seconds
        );
        bars.erase(duplicates.begin(), duplicates.end());
        write_partition(symbol, year, bars);
    }

    std::filesystem::create_directories(marker.parent_path());
    auto temporary = marker;
    temporary += ".tmp";
    {
        std::ofstream output{temporary, std::ios::trunc};
        if (!output) {
            throw std::runtime_error(
                "unable to write legacy-import marker: " + temporary.string()
            );
        }
        output << "legacy symbol CSV imported into year partitions\n";
    }
    std::error_code rename_error;
    std::filesystem::rename(temporary, marker, rename_error);
    if (rename_error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error(
            "unable to atomically mark legacy cache import " + marker.string()
            + ": " + rename_error.message()
        );
    }
}

LegacyMigrationReport MarketDataCsvStore::migrate_legacy_files() const
{
    LegacyMigrationReport report{
        .archive_directory = directory_ / "legacy_flat",
    };
    if (!std::filesystem::exists(directory_)) {
        return report;
    }

    std::vector<std::filesystem::path> legacy_files;
    for (const auto& entry : std::filesystem::directory_iterator{directory_}) {
        if (!entry.is_regular_file() || entry.path().extension() != ".csv") {
            continue;
        }
        const auto filename = entry.path().filename().string();
        if (!filename.empty() && filename.front() != '.') {
            legacy_files.push_back(entry.path());
        }
    }
    std::ranges::sort(legacy_files);

    for (const auto& legacy : legacy_files) {
        const auto symbol = legacy.stem().string();
        static_cast<void>(path_for(symbol));
        const auto legacy_bars = read_bars(legacy);
        std::map<int, std::set<std::int64_t>> expected_timestamps;
        for (const auto& bar : legacy_bars) {
            expected_timestamps[market_year(bar.epoch_seconds)].insert(
                bar.epoch_seconds
            );
        }

        migrate_legacy_partitions(symbol, true);

        for (const auto& [year, expected] : expected_timestamps) {
            const auto partition = partition_path_for(symbol, year);
            if (!std::filesystem::exists(partition)) {
                throw std::runtime_error(
                    "missing year partition after migration: " + partition.string()
                );
            }
            std::set<std::int64_t> actual;
            for (const auto& bar : read_bars(partition)) {
                actual.insert(bar.epoch_seconds);
            }
            if (!std::ranges::includes(actual, expected)) {
                throw std::runtime_error(
                    "year partition failed timestamp verification: "
                    + partition.string()
                );
            }
        }

        std::filesystem::create_directories(report.archive_directory);
        const auto archived = report.archive_directory / legacy.filename();
        if (std::filesystem::exists(archived)) {
            throw std::runtime_error(
                "legacy archive already exists; refusing to overwrite: "
                + archived.string()
            );
        }
        std::error_code rename_error;
        std::filesystem::rename(legacy, archived, rename_error);
        if (rename_error) {
            throw std::runtime_error(
                "unable to archive migrated cache " + legacy.string() + ": "
                + rename_error.message()
            );
        }

        ++report.symbols;
        report.bars += legacy_bars.size();
        report.year_partitions += expected_timestamps.size();
    }
    return report;
}

std::filesystem::path MarketDataCsvStore::completed_sessions_path() const
{
    return directory_ / ".completed_sessions.csv";
}

std::vector<domain::InstrumentBars> MarketDataCsvStore::load(
    std::span<const std::string> symbols
) const
{
    return load_impl(symbols, std::nullopt);
}

std::vector<domain::InstrumentBars> MarketDataCsvStore::load_since(
    std::span<const std::string> symbols,
    std::int64_t first_epoch_seconds
) const
{
    return load_impl(symbols, first_epoch_seconds);
}

std::vector<domain::InstrumentBars> MarketDataCsvStore::load_impl(
    std::span<const std::string> symbols,
    std::optional<std::int64_t> first_epoch_seconds
) const
{
    std::vector<domain::InstrumentBars> instruments;
    instruments.reserve(symbols.size());
    const std::optional<int> first_year = first_epoch_seconds.has_value()
        ? std::optional<int>{market_year(*first_epoch_seconds)}
        : std::nullopt;
    for (const auto& symbol : symbols) {
        domain::InstrumentBars instrument{.symbol = symbol};
        const auto years = partition_years_for(symbol);
        const std::unordered_set<int> partitioned_years{years.begin(), years.end()};

        const auto legacy = path_for(symbol);
        if (std::filesystem::exists(legacy)) {
            for (auto& bar : read_bars(legacy)) {
                if (!partitioned_years.contains(market_year(bar.epoch_seconds))
                    && (!first_epoch_seconds.has_value()
                        || bar.epoch_seconds >= *first_epoch_seconds)) {
                    instrument.bars.push_back(std::move(bar));
                }
            }
        }
        for (const int year : years) {
            if (first_year.has_value() && year < *first_year) {
                continue;
            }
            auto partition = load_partition(symbol, year);
            for (auto& bar : partition.bars) {
                if (!first_epoch_seconds.has_value()
                    || bar.epoch_seconds >= *first_epoch_seconds) {
                    instrument.bars.push_back(std::move(bar));
                }
            }
        }
        instruments.push_back(std::move(instrument));
    }
    return instruments;
}

void MarketDataCsvStore::write_partition(
    const std::string& symbol,
    int year,
    std::span<const domain::MarketBar> bars
) const
{
    const auto path = partition_path_for(symbol, year);
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output{temporary, std::ios::trunc};
        if (!output) {
            throw std::runtime_error(
                "unable to write temporary market-data cache: " + temporary.string()
            );
        }
        output << csv_header << '\n' << std::setprecision(17);
        for (const auto& bar : bars) {
            output << bar.epoch_seconds << ','
                   << bar.open << ',' << bar.high << ',' << bar.low << ','
                   << bar.close << ',';
            write_optional(output, bar.volume);
            output << ',';
            write_optional(output, bar.weighted_average_price);
            output << ',';
            write_optional(output, bar.trade_count);
            output << '\n';
        }
        if (!output) {
            throw std::runtime_error(
                "failed while writing temporary market-data cache: "
                + temporary.string()
            );
        }
    }

    std::error_code rename_error;
    std::filesystem::rename(temporary, path, rename_error);
    if (rename_error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error(
            "unable to atomically replace market-data cache " + path.string()
            + ": " + rename_error.message()
        );
    }
}

void MarketDataCsvStore::save(
    std::span<const domain::InstrumentBars> instruments
) const
{
    for (const auto& instrument : instruments) {
        static_cast<void>(path_for(instrument.symbol));
        std::map<int, std::vector<domain::MarketBar>> partitions;
        for (const auto& bar : instrument.bars) {
            partitions[market_year(bar.epoch_seconds)].push_back(bar);
        }
        for (auto& [year, bars] : partitions) {
            std::ranges::sort(bars, {}, &domain::MarketBar::epoch_seconds);
            const auto duplicates = std::ranges::unique(
                bars, {}, &domain::MarketBar::epoch_seconds
            );
            bars.erase(duplicates.begin(), duplicates.end());
            write_partition(instrument.symbol, year, bars);
        }
    }
}

void MarketDataCsvStore::merge(
    std::span<const domain::InstrumentBars> instruments
) const
{
    std::map<std::pair<std::string, int>, std::vector<domain::MarketBar>> incoming;
    std::unordered_set<std::string> symbols;
    for (const auto& instrument : instruments) {
        static_cast<void>(path_for(instrument.symbol));
        symbols.insert(instrument.symbol);
        for (const auto& bar : instrument.bars) {
            incoming[{instrument.symbol, market_year(bar.epoch_seconds)}].push_back(bar);
        }
    }

    for (const auto& symbol : symbols) {
        migrate_legacy_partitions(symbol);
    }

    for (auto& [key, bars] : incoming) {
        const auto& [symbol, year] = key;
        auto existing = load_partition(symbol, year);
        // Incoming bars are placed first so a repeated timestamp refreshes the
        // older cached value after the stable timestamp sort.
        bars.insert(bars.end(), existing.bars.begin(), existing.bars.end());
        std::stable_sort(
            bars.begin(), bars.end(),
            [](const auto& left, const auto& right) {
                return left.epoch_seconds < right.epoch_seconds;
            }
        );
        const auto duplicates = std::ranges::unique(
            bars, {}, &domain::MarketBar::epoch_seconds
        );
        bars.erase(duplicates.begin(), duplicates.end());
        write_partition(symbol, year, bars);
    }
}

bool MarketDataCsvStore::is_recent(
    std::span<const std::string> symbols,
    std::chrono::hours maximum_age
) const
{
    if (maximum_age <= std::chrono::hours::zero() || symbols.empty()) {
        return false;
    }
    const auto now = std::filesystem::file_time_type::clock::now();
    for (const auto& symbol : symbols) {
        std::vector<std::filesystem::path> paths;
        for (const int year : partition_years_for(symbol)) {
            paths.push_back(partition_path_for(symbol, year));
        }
        const auto legacy = path_for(symbol);
        if (paths.empty() && std::filesystem::exists(legacy)) {
            paths.push_back(legacy);
        }
        if (paths.empty()) {
            return false;
        }
        const auto newest = std::ranges::max(
            paths | std::views::transform([](const auto& path) {
                return std::filesystem::last_write_time(path);
            })
        );
        if (now - newest > maximum_age) {
            return false;
        }
    }
    return true;
}

CompletedMarketSessions MarketDataCsvStore::load_completed_sessions() const
{
    CompletedMarketSessions completed;
    const auto path = completed_sessions_path();
    if (!std::filesystem::exists(path)) {
        return completed;
    }

    std::ifstream input{path};
    if (!input) {
        throw std::runtime_error("unable to open completed-session cache: " + path.string());
    }
    std::string row;
    if (!std::getline(input, row) || row != completed_sessions_header) {
        throw std::runtime_error("invalid completed-session header: " + path.string());
    }
    std::size_t line_number = 1;
    while (std::getline(input, row)) {
        ++line_number;
        if (row.empty()) {
            continue;
        }
        const auto separator = row.find(',');
        if (separator == std::string::npos || row.find(',', separator + 1)
                != std::string::npos) {
            throw std::runtime_error(
                "invalid completed-session row at " + path.string() + ':'
                + std::to_string(line_number)
            );
        }
        const std::string symbol = row.substr(0, separator);
        const std::string market_date = row.substr(separator + 1);
        static_cast<void>(path_for(symbol));
        if (!valid_market_date(market_date)) {
            throw std::runtime_error(
                "invalid completed-session date at " + path.string() + ':'
                + std::to_string(line_number)
            );
        }
        completed[symbol].insert(market_date);
    }
    return completed;
}

void MarketDataCsvStore::mark_session_complete(
    std::string_view symbol,
    std::string_view market_date
) const
{
    const std::string owned_symbol{symbol};
    static_cast<void>(path_for(owned_symbol));
    if (!valid_market_date(market_date)) {
        throw std::invalid_argument("invalid completed-session market date");
    }
    CompletedMarketSessions one;
    one[owned_symbol].insert(std::string{market_date});
    mark_sessions_complete(one);
}

void MarketDataCsvStore::mark_sessions_complete(
    const CompletedMarketSessions& sessions
) const
{
    for (const auto& [symbol, dates] : sessions) {
        static_cast<void>(path_for(symbol));
        for (const auto& date : dates) {
            if (!valid_market_date(date)) {
                throw std::invalid_argument(
                    "invalid completed-session market date"
                );
            }
        }
    }
    auto completed = load_completed_sessions();
    bool changed{};
    for (const auto& [symbol, dates] : sessions) {
        auto& destination = completed[symbol];
        for (const auto& date : dates) {
            changed = destination.insert(date).second || changed;
        }
    }
    if (!changed) {
        return;
    }

    std::filesystem::create_directories(directory_);
    const auto path = completed_sessions_path();
    auto temporary = path;
    temporary += ".tmp";
    std::vector<std::pair<std::string, std::string>> rows;
    for (const auto& [completed_symbol, dates] : completed) {
        for (const auto& date : dates) {
            rows.emplace_back(completed_symbol, date);
        }
    }
    std::ranges::sort(rows);
    {
        std::ofstream output{temporary, std::ios::trunc};
        if (!output) {
            throw std::runtime_error(
                "unable to write temporary completed-session cache: "
                + temporary.string()
            );
        }
        output << completed_sessions_header << '\n';
        for (const auto& [completed_symbol, date] : rows) {
            output << completed_symbol << ',' << date << '\n';
        }
        if (!output) {
            throw std::runtime_error(
                "failed while writing temporary completed-session cache: "
                + temporary.string()
            );
        }
    }

    std::error_code rename_error;
    std::filesystem::rename(temporary, path, rename_error);
    if (rename_error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error(
            "unable to atomically replace completed-session cache " + path.string()
            + ": " + rename_error.message()
        );
    }
}

const std::filesystem::path& MarketDataCsvStore::directory() const noexcept
{
    return directory_;
}

} // namespace daytrader::storage
