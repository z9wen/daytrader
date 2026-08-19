#include "daytrader/storage/SetupOutcomeCsvStore.hpp"

#include <fstream>
#include <iomanip>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace daytrader::storage {
namespace {

constexpr std::string_view csv_header =
    "observed,resolved,signal,trade,kind,rth,score,entry,atr,target,stop,rvol,"
    "rs15_spy,rs30_spy,rs60_spy,rs15_qqq,rs30_qqq,rs60_qqq,delta30,ofi30,"
    "pressure,spread_bps,outcome,mfe_atr,mae_atr,lead_seconds";

[[nodiscard]] std::vector<std::string> split_row(const std::string& row)
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
    return fields;
}

[[nodiscard]] std::optional<double> optional_double(const std::string& value)
{
    return value.empty() ? std::nullopt : std::optional<double>{std::stod(value)};
}

[[nodiscard]] std::optional<std::int64_t> optional_integer(const std::string& value)
{
    return value.empty() ? std::nullopt
                         : std::optional<std::int64_t>{std::stoll(value)};
}

[[nodiscard]] domain::SetupKind parse_kind(const std::string& value)
{
    if (value == "BUILDING") {
        return domain::SetupKind::building;
    }
    if (value == "READY") {
        return domain::SetupKind::ready;
    }
    throw std::invalid_argument("unknown setup kind: " + value);
}

[[nodiscard]] domain::SetupOutcome parse_outcome(const std::string& value)
{
    if (value == "PENDING") {
        return domain::SetupOutcome::pending;
    }
    if (value == "SUCCESS") {
        return domain::SetupOutcome::success;
    }
    if (value == "FAILURE") {
        return domain::SetupOutcome::failure;
    }
    if (value == "AMBIGUOUS") {
        return domain::SetupOutcome::ambiguous;
    }
    throw std::invalid_argument("unknown setup outcome: " + value);
}

void write_optional(std::ostream& output, const std::optional<double>& value)
{
    if (value.has_value()) {
        output << *value;
    }
}

void write_optional(std::ostream& output, const std::optional<std::int64_t>& value)
{
    if (value.has_value()) {
        output << *value;
    }
}

} // namespace

SetupOutcomeCsvStore::SetupOutcomeCsvStore(std::filesystem::path path)
    : path_{std::move(path)}
{
    if (path_.empty()) {
        throw std::invalid_argument("setup-outcome cache path cannot be empty");
    }
}

std::vector<domain::SetupOutcomeRecord> SetupOutcomeCsvStore::load() const
{
    if (!std::filesystem::exists(path_)) {
        return {};
    }
    std::ifstream input{path_};
    std::string row;
    if (!input || !std::getline(input, row) || row != csv_header) {
        throw std::runtime_error("invalid setup-outcome CSV: " + path_.string());
    }

    std::vector<domain::SetupOutcomeRecord> records;
    std::size_t line_number{1};
    while (std::getline(input, row)) {
        ++line_number;
        if (row.empty()) {
            continue;
        }
        const auto fields = split_row(row);
        if (fields.size() != 26) {
            throw std::runtime_error(
                "invalid setup-outcome field count at " + path_.string() + ':'
                + std::to_string(line_number)
            );
        }
        try {
            records.push_back(domain::SetupOutcomeRecord{
                .observed_epoch_seconds = std::stoll(fields[0]),
                .resolved_epoch_seconds = std::stoll(fields[1]),
                .signal_symbol = fields[2],
                .trade_symbol = fields[3],
                .kind = parse_kind(fields[4]),
                .regular_session = fields[5] == "1",
                .bullish_score = std::stoi(fields[6]),
                .entry_price = std::stod(fields[7]),
                .atr = std::stod(fields[8]),
                .target_price = std::stod(fields[9]),
                .stop_price = std::stod(fields[10]),
                .relative_volume = optional_double(fields[11]),
                .rs15_spy = optional_double(fields[12]),
                .rs30_spy = optional_double(fields[13]),
                .rs60_spy = optional_double(fields[14]),
                .rs15_qqq = optional_double(fields[15]),
                .rs30_qqq = optional_double(fields[16]),
                .rs60_qqq = optional_double(fields[17]),
                .delta30 = optional_double(fields[18]),
                .ofi30 = optional_double(fields[19]),
                .combined_pressure = optional_double(fields[20]),
                .spread_basis_points = optional_double(fields[21]),
                .outcome = parse_outcome(fields[22]),
                .maximum_favorable_excursion_atr = std::stod(fields[23]),
                .maximum_adverse_excursion_atr = std::stod(fields[24]),
                .lead_seconds = optional_integer(fields[25]),
            });
        } catch (const std::exception& exception) {
            throw std::runtime_error(
                "invalid setup-outcome value at " + path_.string() + ':'
                + std::to_string(line_number) + ": " + exception.what()
            );
        }
    }
    return records;
}

void SetupOutcomeCsvStore::save(
    std::span<const domain::SetupOutcomeRecord> records
) const
{
    if (!path_.parent_path().empty()) {
        std::filesystem::create_directories(path_.parent_path());
    }
    auto temporary_path = path_;
    temporary_path += ".tmp";
    {
        std::ofstream output{temporary_path, std::ios::trunc};
        if (!output) {
            throw std::runtime_error(
                "unable to write setup-outcome cache: " + temporary_path.string()
            );
        }
        output << csv_header << '\n' << std::setprecision(17);
        for (const auto& record : records) {
            output << record.observed_epoch_seconds << ','
                   << record.resolved_epoch_seconds << ','
                   << record.signal_symbol << ',' << record.trade_symbol << ','
                   << domain::to_string(record.kind) << ','
                   << (record.regular_session ? 1 : 0) << ','
                   << record.bullish_score << ',' << record.entry_price << ','
                   << record.atr << ',' << record.target_price << ','
                   << record.stop_price << ',';
            const std::optional<double> optionals[] = {
                record.relative_volume,
                record.rs15_spy,
                record.rs30_spy,
                record.rs60_spy,
                record.rs15_qqq,
                record.rs30_qqq,
                record.rs60_qqq,
                record.delta30,
                record.ofi30,
                record.combined_pressure,
                record.spread_basis_points,
            };
            for (const auto& value : optionals) {
                write_optional(output, value);
                output << ',';
            }
            output << domain::to_string(record.outcome) << ','
                   << record.maximum_favorable_excursion_atr << ','
                   << record.maximum_adverse_excursion_atr << ',';
            write_optional(output, record.lead_seconds);
            output << '\n';
        }
        if (!output) {
            throw std::runtime_error(
                "failed while writing setup-outcome cache: " + temporary_path.string()
            );
        }
    }
    std::filesystem::rename(temporary_path, path_);
}

const std::filesystem::path& SetupOutcomeCsvStore::path() const noexcept
{
    return path_;
}

} // namespace daytrader::storage
