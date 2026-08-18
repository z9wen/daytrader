#include "daytrader/ibkr/TwsHistoricalTickClient.hpp"

#include "daytrader/ibkr/HistoricalRequestPacer.hpp"

#include "Contract.h"
#include "Decimal.h"
#include "DefaultEWrapper.h"
#include "EClientSocket.h"
#include "EReader.h"
#include "EReaderOSSignal.h"
#include "HistoricalTickBidAsk.h"
#include "HistoricalTickLast.h"
#include "TagValue.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace daytrader::ibkr {
namespace {

constexpr int first_trades_request_id = 7'001;
constexpr int first_bid_ask_request_id = 7'101;

enum class TickStream {
    trades,
    bid_ask,
};

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

[[nodiscard]] double decimal_to_size(Decimal value)
{
    if (value == UNSET_DECIMAL) {
        return 0.0;
    }
    const double converted = DecimalFunctions::decimalToDouble(value);
    return std::isfinite(converted) && converted > 0.0 ? converted : 0.0;
}

[[nodiscard]] std::string format_utc(std::int64_t epoch_seconds)
{
    const std::time_t timestamp = static_cast<std::time_t>(epoch_seconds);
    std::tm utc{};
    if (gmtime_r(&timestamp, &utc) == nullptr) {
        throw std::runtime_error("unable to convert historical-tick timestamp");
    }
    std::array<char, 40> output{};
    if (std::strftime(output.data(), output.size(), "%Y%m%d %H:%M:%S UTC", &utc) == 0) {
        throw std::runtime_error("unable to format historical-tick timestamp");
    }
    return output.data();
}

[[nodiscard]] Contract make_contract(const config::HistoricalDataSettings& settings)
{
    Contract contract;
    contract.symbol = settings.symbol;
    contract.secType = settings.security_type;
    contract.exchange = settings.exchange;
    contract.primaryExchange = settings.primary_exchange;
    contract.currency = settings.currency;
    return contract;
}

} // namespace

class TwsHistoricalTickClient::Impl final : public DefaultEWrapper {
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

    [[nodiscard]] domain::OrderFlowTicks fetch(const HistoricalTickRequest& request)
    {
        prepare(request);
        connect();
        auto deadline = std::chrono::steady_clock::now() + settings_.request_timeout;
        auto observed_activity = activity_counter_.load();
        while (!complete_.load() && !failed_.load()) {
            process_messages();
            const auto current_activity = activity_counter_.load();
            const auto now = std::chrono::steady_clock::now();
            if (current_activity != observed_activity) {
                // Intentional pacing can sleep for several minutes. Only a
                // lack of progress after a request is active should time out.
                observed_activity = current_activity;
                deadline = now + settings_.request_timeout;
            } else if (now >= deadline) {
                fail("timed out waiting for IBKR historical ticks");
            }
        }

        shutdown();
        const auto failure = failure_message();
        if (!failure.empty()) {
            throw std::runtime_error(failure);
        }
        return std::move(result_);
    }

    void nextValidId(int) override
    {
        if (!request_started_.exchange(true)) {
            request_trades_page();
        }
    }

    void historicalTicksLast(
        int request_id,
        const std::vector<HistoricalTickLast>& ticks,
        bool done
    ) override
    {
        if (stream_ != TickStream::trades || request_id != active_request_id_) {
            return;
        }
        ++activity_counter_;
        for (const auto& tick : ticks) {
            const double size = decimal_to_size(tick.size);
            if (tick.time > request_.end_timestamp || size <= 0.0
                || !std::isfinite(tick.price) || tick.price <= 0.0
                || tick.tickAttribLast.pastLimit) {
                continue;
            }
            result_.trades.push_back(domain::TradeTick{
                .epoch_seconds = tick.time,
                .sequence = result_.trades.size(),
                .price = tick.price,
                .size = size,
            });
        }
        if (done && !failed_.load()) {
            if (needs_older_trades()) {
                trades_cursor_ = earliest_trade_timestamp() - 1;
                request_trades_page();
            } else {
                stream_ = TickStream::bid_ask;
                request_bid_ask_page();
            }
        }
    }

    void historicalTicksBidAsk(
        int request_id,
        const std::vector<HistoricalTickBidAsk>& ticks,
        bool done
    ) override
    {
        if (stream_ != TickStream::bid_ask || request_id != active_request_id_) {
            return;
        }
        ++activity_counter_;
        for (const auto& tick : ticks) {
            if (tick.time > request_.end_timestamp) {
                continue;
            }
            result_.quotes.push_back(domain::BidAskTick{
                .epoch_seconds = tick.time,
                .sequence = result_.quotes.size(),
                .bid_price = tick.priceBid,
                .ask_price = tick.priceAsk,
                .bid_size = decimal_to_size(tick.sizeBid),
                .ask_size = decimal_to_size(tick.sizeAsk),
            });
        }
        if (done) {
            if (!failed_.load() && needs_older_quotes()) {
                quotes_cursor_ = earliest_quote_timestamp() - 1;
                request_bid_ask_page();
            } else {
                normalize_result();
                complete_.store(true);
            }
        }
    }

    void error(
        int request_id,
        std::time_t,
        int error_code,
        const std::string& error_text,
        const std::string&
    ) override
    {
        std::ostringstream message;
        message << "IBKR " << error_code << ": " << error_text;
        if (request_id == active_request_id_) {
            message << " [symbol=" << request_.contract.symbol
                    << ", requestId=" << request_id << ']';
        }
        if (is_informational_message(error_code)) {
            std::clog << message.str() << '\n';
            return;
        }
        std::cerr << message.str() << '\n';
        if (request_id == active_request_id_
            || error_code == 502 || error_code == 504 || error_code == 1100
            || error_code == 1101) {
            fail(message.str());
        }
    }

    void connectionClosed() override
    {
        if (!shutting_down_.load()) {
            fail("IBKR connection closed while fetching historical ticks");
        }
    }

private:
    void prepare(const HistoricalTickRequest& request)
    {
        if (reader_ != nullptr || client_.isConnected()) {
            throw std::logic_error("IBKR historical-tick client supports one fetch");
        }
        if (request.contract.symbol.empty()) {
            throw std::invalid_argument("historical-tick symbol cannot be empty");
        }
        if (request.end_timestamp <= 0) {
            throw std::invalid_argument("historical-tick end timestamp must be positive");
        }
        if (request.number_of_ticks <= 0 || request.number_of_ticks > 1'000) {
            throw std::invalid_argument("IBKR historical-tick count must be 1..1000");
        }
        if (request.minimum_lookback_seconds <= 0) {
            throw std::invalid_argument("historical-tick lookback must be positive");
        }
        if (request.maximum_pages_per_stream <= 0
            || request.maximum_pages_per_stream > 20) {
            throw std::invalid_argument("historical-tick page limit must be 1..20");
        }
        request_ = request;
        result_ = domain::OrderFlowTicks{
            .symbol = request.contract.symbol,
            .requested_end_timestamp = request.end_timestamp,
        };
        complete_.store(false);
        failed_.store(false);
        request_started_.store(false);
        activity_counter_.store(0);
        shutting_down_.store(false);
        stream_ = TickStream::trades;
        trades_pages_ = 0;
        quote_pages_ = 0;
        trades_cursor_ = request.end_timestamp;
        quotes_cursor_ = request.end_timestamp;
        active_request_id_ = 0;
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
                "unable to connect to " + settings_.host + ':'
                + std::to_string(settings_.port)
            );
        }
        reader_ = std::make_unique<EReader>(&client_, &signal_);
        reader_->start();
    }

    [[nodiscard]] std::int64_t target_start_timestamp() const
    {
        return request_.end_timestamp - request_.minimum_lookback_seconds + 1;
    }

    [[nodiscard]] std::int64_t earliest_trade_timestamp() const
    {
        const auto found = std::ranges::min_element(
            result_.trades,
            {},
            &domain::TradeTick::epoch_seconds
        );
        return found == result_.trades.end()
            ? request_.end_timestamp
            : found->epoch_seconds;
    }

    [[nodiscard]] std::int64_t earliest_quote_timestamp() const
    {
        const auto found = std::ranges::min_element(
            result_.quotes,
            {},
            &domain::BidAskTick::epoch_seconds
        );
        return found == result_.quotes.end()
            ? request_.end_timestamp
            : found->epoch_seconds;
    }

    [[nodiscard]] bool needs_older_trades() const
    {
        return !result_.trades.empty()
            && earliest_trade_timestamp() > target_start_timestamp()
            && trades_pages_ < request_.maximum_pages_per_stream;
    }

    [[nodiscard]] bool needs_older_quotes() const
    {
        return !result_.quotes.empty()
            && earliest_quote_timestamp() > target_start_timestamp()
            && quote_pages_ < request_.maximum_pages_per_stream;
    }

    void request_trades_page()
    {
        HistoricalRequestPacer::shared().acquire(1);
        active_request_id_ = first_trades_request_id + trades_pages_;
        ++trades_pages_;
        client_.reqHistoricalTicks(
            active_request_id_,
            make_contract(request_.contract),
            {},
            format_utc(trades_cursor_),
            request_.number_of_ticks,
            "TRADES",
            request_.contract.regular_trading_hours_only ? 1 : 0,
            false,
            TagValueListSPtr{}
        );
        ++activity_counter_;
    }

    void request_bid_ask_page()
    {
        // IBKR counts each BID_ASK historical request twice for pacing.
        HistoricalRequestPacer::shared().acquire(2);
        active_request_id_ = first_bid_ask_request_id + quote_pages_;
        ++quote_pages_;
        client_.reqHistoricalTicks(
            active_request_id_,
            make_contract(request_.contract),
            {},
            format_utc(quotes_cursor_),
            request_.number_of_ticks,
            "BID_ASK",
            request_.contract.regular_trading_hours_only ? 1 : 0,
            false,
            TagValueListSPtr{}
        );
        ++activity_counter_;
    }

    void normalize_result()
    {
        std::ranges::stable_sort(
            result_.trades,
            {},
            &domain::TradeTick::epoch_seconds
        );
        for (std::size_t index = 0; index < result_.trades.size(); ++index) {
            result_.trades[index].sequence = index;
        }
        std::ranges::stable_sort(
            result_.quotes,
            {},
            &domain::BidAskTick::epoch_seconds
        );
        for (std::size_t index = 0; index < result_.quotes.size(); ++index) {
            result_.quotes[index].sequence = index;
        }
    }

    void process_messages()
    {
        signal_.waitForSignal();
        try {
            reader_->processMsgs();
        } catch (const std::exception& exception) {
            fail(std::string{"failed to process an IBKR tick message: "}
                 + exception.what());
        }
        if (!client_.isConnected() && !shutting_down_.load() && !failed_.load()) {
            fail("IBKR connection closed while fetching historical ticks");
        }
    }

    void shutdown()
    {
        if (shutting_down_.exchange(true)) {
            return;
        }
        if (client_.isConnected()) {
            if (!complete_.load() && active_request_id_ != 0) {
                client_.cancelHistoricalTicks(active_request_id_);
            }
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
    HistoricalTickRequest request_;
    domain::OrderFlowTicks result_;
    EReaderOSSignal signal_;
    EClientSocket client_;
    std::unique_ptr<EReader> reader_;
    std::atomic_bool complete_{};
    std::atomic_bool failed_{};
    std::atomic_bool request_started_{};
    std::atomic_bool shutting_down_{};
    std::atomic_size_t activity_counter_{};
    int active_request_id_{};
    TickStream stream_{TickStream::trades};
    int trades_pages_{};
    int quote_pages_{};
    std::int64_t trades_cursor_{};
    std::int64_t quotes_cursor_{};
    mutable std::mutex failure_mutex_;
    std::string failure_message_;
};

TwsHistoricalTickClient::TwsHistoricalTickClient(
    config::IbkrConnectionSettings settings
)
    : impl_{std::make_unique<Impl>(std::move(settings))}
{
}

TwsHistoricalTickClient::~TwsHistoricalTickClient() = default;
TwsHistoricalTickClient::TwsHistoricalTickClient(TwsHistoricalTickClient&&) noexcept = default;
TwsHistoricalTickClient& TwsHistoricalTickClient::operator=(
    TwsHistoricalTickClient&&
) noexcept = default;

domain::OrderFlowTicks TwsHistoricalTickClient::fetch(
    const HistoricalTickRequest& request
)
{
    return impl_->fetch(request);
}

} // namespace daytrader::ibkr
