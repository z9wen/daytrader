#include "daytrader/ibkr/TwsLiveContextClient.hpp"

#include "daytrader/live/LiveOrderFlowTracker.hpp"
#include "daytrader/live/PositionTracker.hpp"

#include "CommonDefs.h"
#include "Contract.h"
#include "Decimal.h"
#include "DefaultEWrapper.h"
#include "EClientSocket.h"
#include "EReader.h"
#include "EReaderOSSignal.h"
#include "TagValue.h"
#include "TickAttrib.h"
#include "TickAttribBidAsk.h"
#include "TickAttribLast.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace daytrader::ibkr {
namespace {

constexpr int first_live_tick_request_id = 20'001;
constexpr int first_pnl_request_id = 30'001;
constexpr int first_position_market_request_id = 40'001;

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

[[nodiscard]] std::optional<double> finite_api_double(double value)
{
    if (value == UNSET_DOUBLE || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::string position_key(const std::string& account, int contract_id)
{
    return account + ':' + std::to_string(contract_id);
}

[[nodiscard]] std::int64_t current_epoch_seconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

} // namespace

class TwsLiveContextClient::Impl final : public DefaultEWrapper {
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

    void monitor(
        const std::vector<config::HistoricalDataSettings>& order_flow_symbols,
        const std::function<void(domain::LiveTradeContext)>& on_update,
        const std::function<bool()>& stop_requested
    )
    {
        prepare(order_flow_symbols, on_update);
        if (!stop_requested) {
            throw std::invalid_argument("live-context stop predicate is required");
        }
        connect();
        while (!stop_requested() && !failed_.load()) {
            process_messages();
        }
        shutdown();
        const auto failure = failure_message();
        update_handler_ = {};
        if (!failure.empty()) {
            throw std::runtime_error(failure);
        }
    }

    void nextValidId(int) override
    {
        if (requests_started_.exchange(true)) {
            return;
        }

        client_.reqPositions();
        for (std::size_t index = 0; index < subscriptions_.size(); ++index) {
            const auto& request = subscriptions_[index];
            Contract contract;
            contract.symbol = request.symbol;
            contract.secType = request.security_type;
            contract.exchange = request.exchange;
            contract.primaryExchange = request.primary_exchange;
            contract.currency = request.currency;

            const int trade_id = first_live_tick_request_id
                + static_cast<int>(index * 2);
            const int quote_id = trade_id + 1;
            request_to_tracker_.emplace(trade_id, index);
            request_to_tracker_.emplace(quote_id, index);
            client_.reqTickByTickData(trade_id, contract, "Last", 0, false);
            client_.reqTickByTickData(quote_id, contract, "BidAsk", 0, false);
        }
        order_flow_connected_ = true;
        publish(true);
    }

    void position(
        const std::string& account,
        const Contract& contract,
        Decimal position,
        double average_cost
    ) override
    {
        if (contract.secType != "STK" || position == UNSET_DECIMAL) {
            return;
        }
        const double quantity = DecimalFunctions::decimalToDouble(position);
        positions_.update_position(
            account,
            contract.symbol,
            contract.conId,
            quantity,
            average_cost
        );

        const auto key = position_key(account, contract.conId);
        const auto existing = position_to_pnl_request_.find(key);
        const auto existing_market = position_to_market_request_.find(key);
        if (std::abs(quantity) < 1e-12) {
            if (existing != position_to_pnl_request_.end()) {
                client_.cancelPnLSingle(existing->second);
                pnl_requests_.erase(existing->second);
                position_to_pnl_request_.erase(existing);
            }
            if (existing_market != position_to_market_request_.end()) {
                client_.cancelMktData(existing_market->second);
                market_requests_.erase(existing_market->second);
                position_to_market_request_.erase(existing_market);
            }
        } else if (existing == position_to_pnl_request_.end()) {
            const int request_id = next_pnl_request_id_++;
            position_to_pnl_request_.emplace(key, request_id);
            pnl_requests_.emplace(
                request_id,
                PositionIdentity{
                    .account = account,
                    .symbol = contract.symbol,
                    .contract_id = contract.conId,
                }
            );
            client_.reqPnLSingle(request_id, account, "", contract.conId);
        }
        if (std::abs(quantity) >= 1e-12
            && existing_market == position_to_market_request_.end()) {
            const int request_id = next_position_market_request_id_++;
            auto market_contract = contract;
            market_contract.exchange = "SMART";
            position_to_market_request_.emplace(key, request_id);
            market_requests_.emplace(
                request_id,
                PositionMarketState{
                    .identity = PositionIdentity{
                        .account = account,
                        .symbol = contract.symbol,
                        .contract_id = contract.conId,
                    },
                }
            );
            client_.reqMktData(
                request_id,
                market_contract,
                "",
                false,
                false,
                TagValueListSPtr{}
            );
        }
        publish(true);
    }

    void positionEnd() override
    {
        positions_ready_ = true;
        publish(true);
    }

    void pnlSingle(
        int request_id,
        Decimal,
        double daily_pnl,
        double unrealized_pnl,
        double,
        double value
    ) override
    {
        const auto found = pnl_requests_.find(request_id);
        if (found == pnl_requests_.end()) {
            return;
        }
        positions_.update_pnl(
            found->second.account,
            found->second.contract_id,
            finite_api_double(daily_pnl),
            finite_api_double(unrealized_pnl),
            finite_api_double(value)
        );
        publish(true);
    }

    void tickByTickAllLast(
        int request_id,
        int,
        std::time_t timestamp,
        double price,
        Decimal size,
        const TickAttribLast& attributes,
        const std::string&,
        const std::string&
    ) override
    {
        const auto index = tracker_index(request_id);
        if (!index.has_value() || size == UNSET_DECIMAL || attributes.unreported) {
            return;
        }
        trackers_[*index].on_trade(
            static_cast<std::int64_t>(timestamp),
            price,
            DecimalFunctions::decimalToDouble(size)
        );
        publish(false);
    }

    void tickByTickBidAsk(
        int request_id,
        std::time_t timestamp,
        double bid_price,
        double ask_price,
        Decimal bid_size,
        Decimal ask_size,
        const TickAttribBidAsk&
    ) override
    {
        const auto index = tracker_index(request_id);
        if (!index.has_value() || bid_size == UNSET_DECIMAL || ask_size == UNSET_DECIMAL) {
            return;
        }
        trackers_[*index].on_quote(
            static_cast<std::int64_t>(timestamp),
            bid_price,
            ask_price,
            DecimalFunctions::decimalToDouble(bid_size),
            DecimalFunctions::decimalToDouble(ask_size)
        );
        publish(false);
    }

    void tickPrice(
        int request_id,
        TickType field,
        double price,
        const TickAttrib&
    ) override
    {
        const auto found = market_requests_.find(request_id);
        if (found == market_requests_.end() || !std::isfinite(price) || price <= 0.0) {
            return;
        }
        auto& state = found->second;
        switch (field) {
        case BID:
        case DELAYED_BID:
            state.bid = price;
            break;
        case ASK:
        case DELAYED_ASK:
            state.ask = price;
            break;
        case LAST:
        case DELAYED_LAST:
        case LAST_RTH_TRADE:
            state.last = price;
            break;
        case MARK_PRICE:
            state.mark = price;
            break;
        default:
            break;
        }

        auto selected = state.mark.has_value() ? state.mark : state.last;
        if (!selected.has_value() && state.bid.has_value() && state.ask.has_value()) {
            selected = (*state.bid + *state.ask) / 2.0;
        }
        if (!selected.has_value()) {
            selected = state.bid.has_value() ? state.bid : state.ask;
        }
        if (selected.has_value()) {
            positions_.update_market_price(
                state.identity.account,
                state.identity.contract_id,
                *selected
            );
            publish(false);
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
        message << "IBKR live context " << error_code << ": " << error_text;
        if (is_informational_message(error_code)) {
            std::clog << message.str() << '\n';
            return;
        }
        if (request_to_tracker_.contains(request_id)
            || market_requests_.contains(request_id)) {
            std::clog << message.str() << " (optional live stream unavailable)\n";
            return;
        }
        if (const auto pnl = pnl_requests_.find(request_id);
            pnl != pnl_requests_.end()) {
            std::clog << message.str() << " [symbol=" << pnl->second.symbol
                      << ", conId=" << pnl->second.contract_id
                      << "] (using Level-1 P&L fallback)\n";
            position_to_pnl_request_.erase(position_key(
                pnl->second.account,
                pnl->second.contract_id
            ));
            pnl_requests_.erase(pnl);
            return;
        }
        std::cerr << message.str() << '\n';
        if (error_code == 502 || error_code == 504 || error_code == 1100
            || error_code == 1101) {
            fail(message.str());
        }
    }

    void connectionClosed() override
    {
        if (!shutting_down_.load()) {
            fail("IBKR live-context connection closed");
        }
    }

private:
    struct PositionIdentity {
        std::string account;
        std::string symbol;
        int contract_id{};
    };

    struct PositionMarketState {
        PositionIdentity identity;
        std::optional<double> bid;
        std::optional<double> ask;
        std::optional<double> last;
        std::optional<double> mark;
    };

    void prepare(
        const std::vector<config::HistoricalDataSettings>& order_flow_symbols,
        const std::function<void(domain::LiveTradeContext)>& on_update
    )
    {
        if (!on_update) {
            throw std::invalid_argument("live-context update handler is required");
        }
        if (order_flow_symbols.empty()) {
            throw std::invalid_argument("at least one live Order Flow symbol is required");
        }
        if (reader_ != nullptr || client_.isConnected()) {
            throw std::logic_error("live-context client is already running");
        }

        std::unordered_set<std::string> symbols;
        subscriptions_ = order_flow_symbols;
        trackers_.clear();
        trackers_.reserve(subscriptions_.size());
        for (const auto& subscription : subscriptions_) {
            if (!symbols.insert(subscription.symbol).second) {
                throw std::invalid_argument(
                    "duplicate live Order Flow symbol: " + subscription.symbol
                );
            }
            trackers_.emplace_back(
                subscription.symbol,
                live::LiveOrderFlowSettings{.regular_trading_hours_only = true}
            );
        }
        update_handler_ = on_update;
        request_to_tracker_.clear();
        pnl_requests_.clear();
        position_to_pnl_request_.clear();
        market_requests_.clear();
        position_to_market_request_.clear();
        next_pnl_request_id_ = first_pnl_request_id;
        next_position_market_request_id_ = first_position_market_request_id;
        positions_ready_ = false;
        order_flow_connected_ = false;
        failed_.store(false);
        requests_started_.store(false);
        shutting_down_.store(false);
        last_publish_ = {};
        {
            const std::lock_guard lock{failure_mutex_};
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
                "unable to connect live context to " + settings_.host + ':'
                + std::to_string(settings_.port)
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
            fail(std::string{"failed to process IBKR live context: "} + exception.what());
        }
        if (!client_.isConnected() && !shutting_down_.load() && !failed_.load()) {
            fail("IBKR live-context connection closed");
        }
    }

    [[nodiscard]] std::optional<std::size_t> tracker_index(int request_id) const
    {
        const auto found = request_to_tracker_.find(request_id);
        return found == request_to_tracker_.end()
            ? std::nullopt
            : std::optional<std::size_t>{found->second};
    }

    void publish(bool force)
    {
        if (!update_handler_) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!force && last_publish_ != std::chrono::steady_clock::time_point{}
            && now - last_publish_ < std::chrono::milliseconds{250}) {
            return;
        }
        last_publish_ = now;
        const auto epoch_seconds = current_epoch_seconds();
        domain::LiveTradeContext context{
            .updated_epoch_seconds = epoch_seconds,
            .positions_ready = positions_ready_,
            .order_flow_connected = order_flow_connected_,
            .positions = positions_.snapshot(),
        };
        context.order_flow.reserve(trackers_.size());
        for (const auto& tracker : trackers_) {
            context.order_flow.push_back(tracker.snapshot(epoch_seconds));
        }
        update_handler_(std::move(context));
    }

    void shutdown()
    {
        shutting_down_.store(true);
        if (client_.isConnected()) {
            if (requests_started_.load()) {
                client_.cancelPositions();
                for (const auto& [request_id, index] : request_to_tracker_) {
                    static_cast<void>(index);
                    client_.cancelTickByTickData(request_id);
                }
                for (const auto& [request_id, identity] : pnl_requests_) {
                    static_cast<void>(identity);
                    client_.cancelPnLSingle(request_id);
                }
                for (const auto& [request_id, state] : market_requests_) {
                    static_cast<void>(state);
                    client_.cancelMktData(request_id);
                }
            }
            client_.eDisconnect();
        }
        reader_.reset();
    }

    void fail(std::string message)
    {
        const std::lock_guard lock{failure_mutex_};
        if (failure_message_.empty()) {
            failure_message_ = std::move(message);
        }
        failed_.store(true);
    }

    [[nodiscard]] std::string failure_message() const
    {
        const std::lock_guard lock{failure_mutex_};
        return failure_message_;
    }

    config::IbkrConnectionSettings settings_;
    EReaderOSSignal signal_;
    EClientSocket client_;
    std::unique_ptr<EReader> reader_;
    std::vector<config::HistoricalDataSettings> subscriptions_;
    std::vector<live::LiveOrderFlowTracker> trackers_;
    live::PositionTracker positions_;
    std::unordered_map<int, std::size_t> request_to_tracker_;
    std::unordered_map<int, PositionIdentity> pnl_requests_;
    std::unordered_map<std::string, int> position_to_pnl_request_;
    std::unordered_map<int, PositionMarketState> market_requests_;
    std::unordered_map<std::string, int> position_to_market_request_;
    int next_pnl_request_id_{first_pnl_request_id};
    int next_position_market_request_id_{first_position_market_request_id};
    std::function<void(domain::LiveTradeContext)> update_handler_;
    std::chrono::steady_clock::time_point last_publish_{};
    bool positions_ready_{};
    bool order_flow_connected_{};
    std::atomic_bool failed_{false};
    std::atomic_bool requests_started_{false};
    std::atomic_bool shutting_down_{false};
    mutable std::mutex failure_mutex_;
    std::string failure_message_;
};

TwsLiveContextClient::TwsLiveContextClient(config::IbkrConnectionSettings settings)
    : impl_{std::make_unique<Impl>(std::move(settings))}
{
}

TwsLiveContextClient::~TwsLiveContextClient() = default;
TwsLiveContextClient::TwsLiveContextClient(TwsLiveContextClient&&) noexcept = default;
TwsLiveContextClient& TwsLiveContextClient::operator=(
    TwsLiveContextClient&&
) noexcept = default;

void TwsLiveContextClient::monitor(
    const std::vector<config::HistoricalDataSettings>& order_flow_symbols,
    const std::function<void(domain::LiveTradeContext)>& on_update,
    const std::function<bool()>& stop_requested
)
{
    impl_->monitor(order_flow_symbols, on_update, stop_requested);
}

} // namespace daytrader::ibkr
