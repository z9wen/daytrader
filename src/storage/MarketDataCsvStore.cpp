#include "daytrader/storage/MarketDataCsvStore.hpp"

#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace daytrader::storage {
namespace {

constexpr std::string_view csv_header =
    "epoch_seconds,open,high,low,close,volume,weighted_average_price,trade_count";

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

std::vector<domain::InstrumentBars> MarketDataCsvStore::load(
    std::span<const std::string> symbols
) const
{
    std::vector<domain::InstrumentBars> instruments;
    instruments.reserve(symbols.size());
    for (const auto& symbol : symbols) {
        domain::InstrumentBars instrument{.symbol = symbol};
        const auto path = path_for(symbol);
        if (!std::filesystem::exists(path)) {
            instruments.push_back(std::move(instrument));
            continue;
        }

        std::ifstream input{path};
        if (!input) {
            throw std::runtime_error("unable to open market-data cache: " + path.string());
        }
        std::string row;
        if (!std::getline(input, row) || row != csv_header) {
            throw std::runtime_error("invalid market-data CSV header: " + path.string());
        }
        std::size_t line_number = 1;
        while (std::getline(input, row)) {
            ++line_number;
            if (!row.empty()) {
                instrument.bars.push_back(parse_bar(row, path, line_number));
            }
        }
        instruments.push_back(std::move(instrument));
    }
    return instruments;
}

void MarketDataCsvStore::save(
    std::span<const domain::InstrumentBars> instruments
) const
{
    std::filesystem::create_directories(directory_);
    for (const auto& instrument : instruments) {
        const auto path = path_for(instrument.symbol);
        std::ofstream output{path, std::ios::trunc};
        if (!output) {
            throw std::runtime_error("unable to write market-data cache: " + path.string());
        }
        output << csv_header << '\n' << std::setprecision(17);
        for (const auto& bar : instrument.bars) {
            output << bar.epoch_seconds << ','
                   << bar.open << ',' << bar.high << ',' << bar.low << ',' << bar.close << ',';
            write_optional(output, bar.volume);
            output << ',';
            write_optional(output, bar.weighted_average_price);
            output << ',';
            write_optional(output, bar.trade_count);
            output << '\n';
        }
        if (!output) {
            throw std::runtime_error("failed while writing market-data cache: " + path.string());
        }
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
        const auto path = path_for(symbol);
        if (!std::filesystem::exists(path)
            || now - std::filesystem::last_write_time(path) > maximum_age) {
            return false;
        }
    }
    return true;
}

const std::filesystem::path& MarketDataCsvStore::directory() const noexcept
{
    return directory_;
}

} // namespace daytrader::storage
