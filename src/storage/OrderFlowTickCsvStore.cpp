#include "daytrader/storage/OrderFlowTickCsvStore.hpp"

#include <array>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace daytrader::storage {
namespace {

constexpr std::string_view trade_header = "epoch_seconds,sequence,price,size";
constexpr std::string_view quote_header =
    "epoch_seconds,sequence,bid_price,ask_price,bid_size,ask_size";

template <std::size_t Size>
[[nodiscard]] std::array<std::string, Size> split_row(
    const std::string& row,
    const std::filesystem::path& path,
    std::size_t line_number
)
{
    std::array<std::string, Size> fields;
    std::size_t begin{};
    for (std::size_t index = 0; index + 1 < Size; ++index) {
        const auto separator = row.find(',', begin);
        if (separator == std::string::npos) {
            throw std::runtime_error(
                "invalid order-flow CSV row at " + path.string() + ':'
                + std::to_string(line_number)
            );
        }
        fields[index] = row.substr(begin, separator - begin);
        begin = separator + 1;
    }
    fields.back() = row.substr(begin);
    if (fields.back().find(',') != std::string::npos) {
        throw std::runtime_error(
            "too many order-flow CSV fields at " + path.string() + ':'
            + std::to_string(line_number)
        );
    }
    return fields;
}

} // namespace

OrderFlowTickCsvStore::OrderFlowTickCsvStore(std::filesystem::path directory)
    : directory_{std::move(directory)}
{
    if (directory_.empty()) {
        throw std::invalid_argument("order-flow cache directory cannot be empty");
    }
}

std::filesystem::path OrderFlowTickCsvStore::path_for(
    const std::string& symbol,
    std::int64_t end_timestamp,
    const char* stream
) const
{
    if (symbol.empty() || symbol.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")
            != std::string::npos) {
        throw std::invalid_argument("invalid order-flow cache symbol: " + symbol);
    }
    if (end_timestamp <= 0) {
        throw std::invalid_argument("order-flow cache timestamp must be positive");
    }
    return directory_ / (symbol + '_' + std::to_string(end_timestamp) + '_' + stream + ".csv");
}

std::optional<domain::OrderFlowTicks> OrderFlowTickCsvStore::load(
    const std::string& symbol,
    std::int64_t end_timestamp
) const
{
    const auto trade_path = path_for(symbol, end_timestamp, "trades");
    const auto quote_path = path_for(symbol, end_timestamp, "quotes");
    if (!std::filesystem::exists(trade_path) || !std::filesystem::exists(quote_path)) {
        return std::nullopt;
    }

    domain::OrderFlowTicks result{
        .symbol = symbol,
        .requested_end_timestamp = end_timestamp,
    };
    std::ifstream trade_input{trade_path};
    std::string row;
    if (!trade_input || !std::getline(trade_input, row) || row != trade_header) {
        throw std::runtime_error("invalid order-flow trade CSV: " + trade_path.string());
    }
    std::size_t line_number = 1;
    while (std::getline(trade_input, row)) {
        ++line_number;
        if (row.empty()) {
            continue;
        }
        const auto fields = split_row<4>(row, trade_path, line_number);
        try {
            result.trades.push_back(domain::TradeTick{
                .epoch_seconds = std::stoll(fields[0]),
                .sequence = static_cast<std::size_t>(std::stoull(fields[1])),
                .price = std::stod(fields[2]),
                .size = std::stod(fields[3]),
            });
        } catch (const std::exception& exception) {
            throw std::runtime_error(
                "invalid order-flow trade value at " + trade_path.string() + ':'
                + std::to_string(line_number) + ": " + exception.what()
            );
        }
    }

    std::ifstream quote_input{quote_path};
    if (!quote_input || !std::getline(quote_input, row) || row != quote_header) {
        throw std::runtime_error("invalid order-flow quote CSV: " + quote_path.string());
    }
    line_number = 1;
    while (std::getline(quote_input, row)) {
        ++line_number;
        if (row.empty()) {
            continue;
        }
        const auto fields = split_row<6>(row, quote_path, line_number);
        try {
            result.quotes.push_back(domain::BidAskTick{
                .epoch_seconds = std::stoll(fields[0]),
                .sequence = static_cast<std::size_t>(std::stoull(fields[1])),
                .bid_price = std::stod(fields[2]),
                .ask_price = std::stod(fields[3]),
                .bid_size = std::stod(fields[4]),
                .ask_size = std::stod(fields[5]),
            });
        } catch (const std::exception& exception) {
            throw std::runtime_error(
                "invalid order-flow quote value at " + quote_path.string() + ':'
                + std::to_string(line_number) + ": " + exception.what()
            );
        }
    }
    return result;
}

void OrderFlowTickCsvStore::save(const domain::OrderFlowTicks& ticks) const
{
    std::filesystem::create_directories(directory_);
    const auto trade_path = path_for(
        ticks.symbol,
        ticks.requested_end_timestamp,
        "trades"
    );
    const auto quote_path = path_for(
        ticks.symbol,
        ticks.requested_end_timestamp,
        "quotes"
    );

    std::ofstream trade_output{trade_path, std::ios::trunc};
    if (!trade_output) {
        throw std::runtime_error("unable to write order-flow cache: " + trade_path.string());
    }
    trade_output << trade_header << '\n' << std::setprecision(17);
    for (const auto& trade : ticks.trades) {
        trade_output << trade.epoch_seconds << ',' << trade.sequence << ','
                     << trade.price << ',' << trade.size << '\n';
    }

    std::ofstream quote_output{quote_path, std::ios::trunc};
    if (!quote_output) {
        throw std::runtime_error("unable to write order-flow cache: " + quote_path.string());
    }
    quote_output << quote_header << '\n' << std::setprecision(17);
    for (const auto& quote : ticks.quotes) {
        quote_output << quote.epoch_seconds << ',' << quote.sequence << ','
                     << quote.bid_price << ',' << quote.ask_price << ','
                     << quote.bid_size << ',' << quote.ask_size << '\n';
    }
    if (!trade_output || !quote_output) {
        throw std::runtime_error("failed while writing order-flow cache for " + ticks.symbol);
    }
}

const std::filesystem::path& OrderFlowTickCsvStore::directory() const noexcept
{
    return directory_;
}

} // namespace daytrader::storage
