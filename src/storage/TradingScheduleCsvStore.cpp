#include "daytrader/storage/TradingScheduleCsvStore.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace daytrader::storage {
namespace {

constexpr std::string_view csv_header =
    "market_date,start_datetime,end_datetime,time_zone";

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

[[nodiscard]] int market_year(std::string_view market_date)
{
    if (!valid_market_date(market_date)) {
        throw std::invalid_argument("invalid schedule market date");
    }
    int year{};
    const auto [end, error] = std::from_chars(
        market_date.data(), market_date.data() + 4, year
    );
    if (error != std::errc{} || end != market_date.data() + 4) {
        throw std::invalid_argument("invalid schedule market year");
    }
    return year;
}

[[nodiscard]] std::vector<std::string> split_row(
    const std::string& row,
    const std::filesystem::path& path,
    std::size_t line_number
)
{
    std::vector<std::string> fields;
    std::size_t begin{};
    while (true) {
        const auto separator = row.find(',', begin);
        if (separator == std::string::npos) {
            fields.push_back(row.substr(begin));
            break;
        }
        fields.push_back(row.substr(begin, separator - begin));
        begin = separator + 1;
    }
    if (fields.size() != 4) {
        throw std::runtime_error(
            "invalid trading-schedule CSV row at " + path.string() + ':'
            + std::to_string(line_number)
        );
    }
    return fields;
}

} // namespace

TradingScheduleCsvStore::TradingScheduleCsvStore(std::filesystem::path directory)
    : directory_{std::move(directory)}
{
    if (directory_.empty()) {
        throw std::invalid_argument("trading-schedule directory cannot be empty");
    }
}

std::filesystem::path TradingScheduleCsvStore::path_for(
    const std::string& symbol,
    int year
) const
{
    if (symbol.empty() || symbol.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")
            != std::string::npos) {
        throw std::invalid_argument("invalid schedule symbol: " + symbol);
    }
    if (year < 1900 || year > 3000) {
        throw std::invalid_argument("invalid schedule partition year");
    }
    return directory_ / symbol / (std::to_string(year) + ".csv");
}

bool TradingScheduleCsvStore::has_year(const std::string& symbol, int year) const
{
    return std::filesystem::is_regular_file(path_for(symbol, year));
}

std::filesystem::path TradingScheduleCsvStore::coverage_path_for(
    const std::string& symbol,
    int year
) const
{
    auto path = path_for(symbol, year);
    path.replace_extension(".coverage");
    return path;
}

bool TradingScheduleCsvStore::covers_through(
    const std::string& symbol,
    int year,
    std::string_view market_date
) const
{
    if (market_year(market_date) != year || !has_year(symbol, year)) {
        return false;
    }
    std::ifstream input{coverage_path_for(symbol, year)};
    std::string covered_through;
    if (!input || !std::getline(input, covered_through)
        || !valid_market_date(covered_through)
        || market_year(covered_through) != year) {
        return false;
    }
    return covered_through >= market_date;
}

domain::TradingSchedule TradingScheduleCsvStore::load_year(
    const std::string& symbol,
    int year
) const
{
    domain::TradingSchedule result{.symbol = symbol};
    const auto path = path_for(symbol, year);
    if (!std::filesystem::exists(path)) {
        return result;
    }
    std::ifstream input{path};
    if (!input) {
        throw std::runtime_error("unable to open trading schedule: " + path.string());
    }
    std::string row;
    if (!std::getline(input, row) || row != csv_header) {
        throw std::runtime_error("invalid trading-schedule CSV header: " + path.string());
    }
    std::size_t line_number = 1;
    while (std::getline(input, row)) {
        ++line_number;
        if (row.empty()) {
            continue;
        }
        auto fields = split_row(row, path, line_number);
        if (!valid_market_date(fields[0]) || market_year(fields[0]) != year) {
            throw std::runtime_error(
                "trading-schedule date does not match its year partition at "
                + path.string() + ':' + std::to_string(line_number)
            );
        }
        result.sessions.push_back(domain::TradingSession{
            .market_date = std::move(fields[0]),
            .start_datetime = std::move(fields[1]),
            .end_datetime = std::move(fields[2]),
            .time_zone = std::move(fields[3]),
        });
    }
    return result;
}

domain::TradingSchedule TradingScheduleCsvStore::load_range(
    const std::string& symbol,
    std::string_view first_market_date,
    std::string_view last_market_date
) const
{
    if (!valid_market_date(first_market_date)
        || !valid_market_date(last_market_date)
        || first_market_date > last_market_date) {
        throw std::invalid_argument("invalid trading-schedule date range");
    }
    domain::TradingSchedule result{.symbol = symbol};
    const int first_year = market_year(first_market_date);
    const int last_year = market_year(last_market_date);
    for (int year = first_year; year <= last_year; ++year) {
        auto partition = load_year(symbol, year);
        for (auto& session : partition.sessions) {
            if (session.market_date >= first_market_date
                && session.market_date <= last_market_date) {
                result.sessions.push_back(std::move(session));
            }
        }
    }
    std::ranges::sort(result.sessions, {}, &domain::TradingSession::market_date);
    return result;
}

void TradingScheduleCsvStore::save_year(
    const std::string& symbol,
    int year,
    std::span<const domain::TradingSession> sessions,
    std::string_view covered_through
) const
{
    if (market_year(covered_through) != year) {
        throw std::invalid_argument(
            "schedule coverage date does not belong to target year"
        );
    }
    std::vector<domain::TradingSession> normalized{sessions.begin(), sessions.end()};
    for (const auto& session : normalized) {
        if (market_year(session.market_date) != year) {
            throw std::invalid_argument(
                "trading session does not belong to target year partition"
            );
        }
        if (session.start_datetime.find(',') != std::string::npos
            || session.end_datetime.find(',') != std::string::npos
            || session.time_zone.find(',') != std::string::npos) {
            throw std::invalid_argument("trading schedule fields cannot contain commas");
        }
    }
    std::stable_sort(
        normalized.begin(), normalized.end(),
        [](const auto& left, const auto& right) {
            return left.market_date < right.market_date;
        }
    );
    const auto duplicates = std::ranges::unique(
        normalized, {}, &domain::TradingSession::market_date
    );
    normalized.erase(duplicates.begin(), duplicates.end());

    const auto path = path_for(symbol, year);
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output{temporary, std::ios::trunc};
        if (!output) {
            throw std::runtime_error(
                "unable to write temporary trading schedule: " + temporary.string()
            );
        }
        output << csv_header << '\n';
        for (const auto& session : normalized) {
            output << session.market_date << ',' << session.start_datetime << ','
                   << session.end_datetime << ',' << session.time_zone << '\n';
        }
        if (!output) {
            throw std::runtime_error(
                "failed while writing temporary trading schedule: "
                + temporary.string()
            );
        }
    }
    std::error_code rename_error;
    std::filesystem::rename(temporary, path, rename_error);
    if (rename_error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error(
            "unable to atomically replace trading schedule " + path.string()
            + ": " + rename_error.message()
        );
    }

    // The coverage marker is committed last. If the process stops between the
    // CSV and marker writes, the next run safely refreshes this year.
    const auto coverage_path = coverage_path_for(symbol, year);
    auto temporary_coverage = coverage_path;
    temporary_coverage += ".tmp";
    {
        std::ofstream output{temporary_coverage, std::ios::trunc};
        if (!output) {
            throw std::runtime_error(
                "unable to write schedule coverage marker: "
                + temporary_coverage.string()
            );
        }
        output << covered_through << '\n';
    }
    rename_error.clear();
    std::filesystem::rename(temporary_coverage, coverage_path, rename_error);
    if (rename_error) {
        std::filesystem::remove(temporary_coverage);
        throw std::runtime_error(
            "unable to atomically replace schedule coverage marker "
            + coverage_path.string() + ": " + rename_error.message()
        );
    }
}

const std::filesystem::path& TradingScheduleCsvStore::directory() const noexcept
{
    return directory_;
}

} // namespace daytrader::storage
