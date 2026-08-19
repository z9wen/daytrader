#include "daytrader/ibkr/TwsMarketDataClient.hpp"

#include "daytrader/ibkr/IbkrErrorClassifier.hpp"
#include "daytrader/ibkr/IbkrApiCompatibility.hpp"
#include "daytrader/market_data/CompletedBarSynchronizer.hpp"

#include "Contract.h"
#include "Decimal.h"
#include "DefaultEWrapper.h"
#include "EClientSocket.h"
#include "EReader.h"
#include "EReaderOSSignal.h"
#include "TagValue.h"
#include "bar.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <ctime>
#include <array>
#include <iostream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace daytrader::ibkr {
namespace {

constexpr int first_historical_request_id = 1001;

[[nodiscard]] bool is_informational_message(int error_code)
{
    switch (error_code) {
    case 2104:
    case 2106:
    case 2107:
    case 2108:
    case 2158:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] std::optional<double> decimal_to_optional(Decimal value)
{
    if (is_unset_decimal(value)) {
        return std::nullopt;
    }

    const double converted = DecimalFunctions::decimalToDouble(value);
    return converted < 0.0 ? std::nullopt : std::optional<double>{converted};
}

[[nodiscard]] std::string historical_end_time(std::chrono::minutes delay)
{
    if (delay == std::chrono::minutes::zero()) {
        return {};
    }

    const auto end = std::chrono::system_clock::now() - delay;
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(end);
    std::tm utc{};
    if (gmtime_r(&timestamp, &utc) == nullptr) {
        throw std::runtime_error("unable to calculate historical-data end time");
    }

    std::array<char, 32> output{};
    if (std::strftime(output.data(), output.size(), "%Y%m%d-%H:%M:%S", &utc) == 0) {
        throw std::runtime_error("unable to format historical-data end time");
    }
    return output.data();
}

} // namespace

class TwsMarketDataClient::Impl final : public DefaultEWrapper {
public:
    explicit Impl(config::IbkrConnectionSettings settings)
        : settings_{std::move(settings)}
        , signal_{250}
        , client_{this, &signal_}
    {
    }

    ~Impl() override
    {
        shutdown();
    }

    [[nodiscard]] std::vector<domain::InstrumentBars> fetch(
        const std::vector<config::HistoricalDataSettings>& requests,
        const std::function<bool()>& stop_requested
    )
    {
        prepare(requests, false);
        connect();

        while (!(stop_requested && stop_requested())
               && !initial_data_complete_.load() && !failed_.load()) {
            process_messages();
        }

        shutdown();

        const auto failure = failure_message();
        if (!failure.empty()) {
            throw std::runtime_error(failure);
        }

        return std::move(results_);
    }

    void monitor(
        const std::vector<config::HistoricalDataSettings>& requests,
        const std::vector<std::string>& synchronization_symbols,
        std::chrono::seconds bar_interval,
        const std::function<void(const std::vector<domain::InstrumentBars>&)>& on_completed_bar,
        const std::function<bool()>& stop_requested
    )
    {
        if (bar_interval <= std::chrono::seconds::zero()) {
            throw std::invalid_argument("bar interval must be positive");
        }
        if (!on_completed_bar) {
            throw std::invalid_argument("completed-bar handler is required");
        }
        if (!stop_requested) {
            throw std::invalid_argument("stop predicate is required");
        }
        if (synchronization_symbols.empty()) {
            throw std::invalid_argument("at least one synchronization symbol is required");
        }

        for (const auto& request : requests) {
            if (request.end_delay != std::chrono::minutes::zero()) {
                throw std::invalid_argument(
                    "continuous historical data requires a zero end delay"
                );
            }
        }

        bar_interval_ = bar_interval;
        completed_bar_handler_ = on_completed_bar;
        prepare(requests, true);
        synchronized_results_.clear();
        synchronized_results_.reserve(synchronization_symbols.size());
        for (const auto& symbol : synchronization_symbols) {
            const auto found = std::ranges::find(results_, symbol, &domain::InstrumentBars::symbol);
            if (found == results_.end()) {
                throw std::invalid_argument("synchronization symbol is not requested: " + symbol);
            }
            synchronized_results_.push_back(&*found);
        }
        connect();

        while (!stop_requested() && !failed_.load()) {
            process_messages();
        }

        shutdown();
        const auto failure = failure_message();
        completed_bar_handler_ = {};
        if (!failure.empty()) {
            throw std::runtime_error(failure);
        }
    }

    void nextValidId(int) override
    {
        if (request_started_.exchange(true)) {
            return;
        }

        for (std::size_t index = 0; index < requests_.size(); ++index) {
            const auto& request = requests_[index];

            Contract contract;
            contract.symbol = request.symbol;
            contract.secType = request.security_type;
            contract.exchange = request.exchange;
            contract.primaryExchange = request.primary_exchange;
            contract.currency = request.currency;

            client_.reqHistoricalData(
                first_historical_request_id + static_cast<int>(index),
                contract,
                keep_up_to_date_ ? std::string{} : historical_end_time(request.end_delay),
                request.duration,
                request.bar_size,
                request.data_type,
                request.regular_trading_hours_only ? 1 : 0,
                2,
                keep_up_to_date_,
                TagValueListSPtr{}
            );
        }
    }

    void historicalData(int request_id, const Bar& bar) override
    {
        const auto index = request_index(request_id);
        if (!index.has_value()) {
            return;
        }
        store_bar(*index, bar);
    }

    void historicalDataUpdate(int request_id, const Bar& bar) override
    {
        const auto index = request_index(request_id);
        if (!index.has_value() || !keep_up_to_date_) {
            return;
        }
        store_bar(*index, bar);
        publish_completed_bar();
    }

    void historicalDataEnd(int request_id, const std::string&, const std::string&) override
    {
        const auto index = request_index(request_id);
        if (index.has_value()) {
            complete_initial_request(*index);
        }
    }

    void error(
        int request_id,
        IbkrErrorTime,
        int error_code,
        const std::string& error_text,
        const std::string&
    ) override
    {
        std::ostringstream message;
        message << "IBKR " << error_code << ": " << error_text;
        if (const auto index = request_index(request_id); index.has_value()) {
            message << " [symbol=" << requests_[*index].symbol
                    << ", requestId=" << request_id << ']';
        }

        if (is_informational_message(error_code)) {
            std::clog << message.str() << '\n';
            return;
        }

        if (is_pacing_or_rate_limit_error(error_code, error_text)) {
            std::cerr << message.str() << " (caller will retry)\n";
            fail(message.str());
            return;
        }

        if (const auto index = request_index(request_id);
            index.has_value() && !requests_[*index].required) {
            std::clog << message.str() << " (optional request unavailable; continuing)\n";
            complete_initial_request(*index);
            return;
        }

        std::cerr << message.str() << '\n';
        if (request_index(request_id).has_value() || error_code == 502 || error_code == 504
            || error_code == 1100 || error_code == 1101) {
            fail(message.str());
        }
    }

    void connectionClosed() override
    {
        if (!shutting_down_.load()) {
            fail("IBKR connection closed");
        }
    }

private:
    void complete_initial_request(std::size_t index)
    {
        if (!ended_requests_[index]) {
            ended_requests_[index] = true;
            ++completed_requests_;
        }
        if (completed_requests_ == requests_.size()) {
            initial_data_complete_.store(true);
            publish_completed_bar();
        }
    }

    void prepare(
        const std::vector<config::HistoricalDataSettings>& requests,
        bool keep_up_to_date
    )
    {
        if (reader_ != nullptr || client_.isConnected()) {
            throw std::logic_error("IBKR client instances support one request at a time");
        }
        if (requests.empty()) {
            throw std::invalid_argument("at least one historical-data request is required");
        }

        std::unordered_set<std::string> symbols;
        for (const auto& request : requests) {
            if (request.symbol.empty()) {
                throw std::invalid_argument("historical-data symbols cannot be empty");
            }
            if (!symbols.insert(request.symbol).second) {
                throw std::invalid_argument("duplicate historical-data symbol: " + request.symbol);
            }
        }

        requests_ = requests;
        results_.clear();
        results_.reserve(requests_.size());
        for (const auto& request : requests_) {
            results_.push_back(domain::InstrumentBars{.symbol = request.symbol});
        }
        ended_requests_.assign(requests_.size(), false);
        completed_requests_ = 0;
        keep_up_to_date_ = keep_up_to_date;
        last_published_bar_.reset();
        initial_data_complete_.store(false);
        failed_.store(false);
        request_started_.store(false);
        shutting_down_.store(false);
        {
            std::lock_guard lock{failure_mutex_};
            failure_message_.clear();
        }
    }

    void connect()
    {
        if (!client_.eConnect(
                settings_.host.c_str(),
                settings_.port,
                settings_.client_id,
                false
            )) {
            throw std::runtime_error(
                "unable to connect to " + settings_.host + ':' + std::to_string(settings_.port)
            );
        }

        reader_ = std::make_unique<EReader>(&client_, &signal_);
        reader_->start();
    }

    void process_messages()
    {
        signal_.waitForSignal();
        try {
            reader_->processMsgs();
        } catch (const std::exception& exception) {
            fail(std::string{"failed to process an IBKR message: "} + exception.what());
        }

        if (!client_.isConnected() && !shutting_down_.load() && !failed_.load()) {
            fail("IBKR connection closed");
        }
    }

    void store_bar(std::size_t index, const Bar& bar)
    {
        std::int64_t epoch_seconds{};
        const auto [end, error] = std::from_chars(
            bar.time.data(),
            bar.time.data() + bar.time.size(),
            epoch_seconds
        );
        if (error != std::errc{} || end != bar.time.data() + bar.time.size()) {
            fail("IBKR returned an unexpected bar timestamp: " + bar.time);
            return;
        }

        domain::MarketBar converted{
            .epoch_seconds = epoch_seconds,
            .open = bar.open,
            .high = bar.high,
            .low = bar.low,
            .close = bar.close,
            .volume = decimal_to_optional(bar.volume),
            .weighted_average_price = decimal_to_optional(bar.wap),
            .trade_count = bar.count < 0 ? std::nullopt : std::optional<int>{bar.count},
        };

        auto& bars = results_[index].bars;
        const auto found = std::lower_bound(
            bars.begin(),
            bars.end(),
            epoch_seconds,
            [](const domain::MarketBar& existing, std::int64_t timestamp) {
                return existing.epoch_seconds < timestamp;
            }
        );
        if (found != bars.end() && found->epoch_seconds == epoch_seconds) {
            *found = std::move(converted);
        } else {
            bars.insert(found, std::move(converted));
        }
    }

    void publish_completed_bar()
    {
        if (!keep_up_to_date_ || !initial_data_complete_.load()
            || !completed_bar_handler_) {
            return;
        }

        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        const auto completed = market_data::latest_common_completed_bar(
            synchronized_results_,
            bar_interval_,
            now
        );
        if (!completed.has_value()
            || (last_published_bar_.has_value() && *completed <= *last_published_bar_)) {
            return;
        }

        completed_bar_handler_(results_);
        last_published_bar_ = completed;
    }

    [[nodiscard]] std::optional<std::size_t> request_index(int request_id) const
    {
        const int offset = request_id - first_historical_request_id;
        if (offset < 0 || static_cast<std::size_t>(offset) >= requests_.size()) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(offset);
    }

    void shutdown()
    {
        shutting_down_.store(true);
        if (client_.isConnected()) {
            client_.eDisconnect();
        }
        reader_.reset();
    }

    void fail(std::string message)
    {
        {
            std::lock_guard lock{failure_mutex_};
            if (failure_message_.empty()) {
                failure_message_ = std::move(message);
            }
        }
        failed_.store(true);
    }

    [[nodiscard]] std::string failure_message() const
    {
        std::lock_guard lock{failure_mutex_};
        return failure_message_;
    }

    config::IbkrConnectionSettings settings_;
    std::vector<config::HistoricalDataSettings> requests_;
    EReaderOSSignal signal_;
    EClientSocket client_;
    std::unique_ptr<EReader> reader_;
    std::vector<domain::InstrumentBars> results_;
    std::vector<bool> ended_requests_;
    std::size_t completed_requests_{};
    bool keep_up_to_date_{};
    std::chrono::seconds bar_interval_{std::chrono::minutes{5}};
    std::function<void(const std::vector<domain::InstrumentBars>&)> completed_bar_handler_;
    std::vector<const domain::InstrumentBars*> synchronized_results_;
    std::optional<std::int64_t> last_published_bar_;
    std::atomic_bool initial_data_complete_{false};
    std::atomic_bool failed_{false};
    std::atomic_bool request_started_{false};
    std::atomic_bool shutting_down_{false};
    mutable std::mutex failure_mutex_;
    std::string failure_message_;
};

TwsMarketDataClient::TwsMarketDataClient(config::IbkrConnectionSettings settings)
    : impl_{std::make_unique<Impl>(std::move(settings))}
{
}

TwsMarketDataClient::~TwsMarketDataClient() = default;
TwsMarketDataClient::TwsMarketDataClient(TwsMarketDataClient&&) noexcept = default;
TwsMarketDataClient& TwsMarketDataClient::operator=(TwsMarketDataClient&&) noexcept = default;

std::vector<domain::InstrumentBars> TwsMarketDataClient::fetch_historical_bars(
    const std::vector<config::HistoricalDataSettings>& requests,
    const std::function<bool()>& stop_requested
)
{
    return impl_->fetch(requests, stop_requested);
}

void TwsMarketDataClient::monitor_historical_bars(
    const std::vector<config::HistoricalDataSettings>& requests,
    const std::vector<std::string>& synchronization_symbols,
    std::chrono::seconds bar_interval,
    const std::function<void(const std::vector<domain::InstrumentBars>&)>& on_completed_bar,
    const std::function<bool()>& stop_requested
)
{
    impl_->monitor(
        requests,
        synchronization_symbols,
        bar_interval,
        on_completed_bar,
        stop_requested
    );
}

} // namespace daytrader::ibkr
