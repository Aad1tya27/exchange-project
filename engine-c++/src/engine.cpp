#include "engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace whiskers {

namespace {

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\n\r");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\n\r");
    return value.substr(begin, end - begin + 1);
}

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

Engine::Engine(const EngineEvents& events) : events_(events) {
    const bool withSnapshot = std::getenv("WITH_SNAPSHOT") != nullptr;
    if (withSnapshot && std::filesystem::exists("snapshot.txt")) {
        loadSnapshot("snapshot.txt");
    } else {
        orderbooks_.emplace_back("TATA", std::vector<Order>{}, std::vector<Order>{}, 0, 0);
        setBaseBalances();
    }
}

void Engine::process(const ApiMessage& message, const std::string& clientId) {
    std::visit(Overloaded {
        [&](const CreateOrderRequest& request) { handleCreateOrder(request, clientId); },
        [&](const CancelOrderRequest& request) { handleCancelOrder(request, clientId); },
        [&](const OnRampRequest& request) { handleOnRamp(request); },
        [&](const GetDepthRequest& request) { handleGetDepth(request, clientId); },
        [&](const GetOpenOrdersRequest& request) { handleGetOpenOrders(request, clientId); },
    }, message);
}

void Engine::addOrderbook(Orderbook orderbook) {
    orderbooks_.push_back(std::move(orderbook));
}

void Engine::saveSnapshot(const std::string& path) const {
    std::ofstream output(path);
    if (!output.is_open()) {
        throw std::runtime_error("Unable to open snapshot file for writing");
    }

    output << "ENGINE_SNAPSHOT_V1\n";
    for (const auto& orderbook : orderbooks_) {
        const auto snapshot = orderbook.getSnapshot();
        output << "ORDERBOOK " << snapshot.baseAsset << ' ' << snapshot.lastTradeId << ' ' << snapshot.currentPrice << '\n';
        for (const auto& order : snapshot.bids) {
            output << "BID " << order.price << ' ' << order.quantity << ' ' << order.orderId << ' ' << order.filled << ' ' << toString(order.side) << ' ' << order.userId << '\n';
        }
        for (const auto& order : snapshot.asks) {
            output << "ASK " << order.price << ' ' << order.quantity << ' ' << order.orderId << ' ' << order.filled << ' ' << toString(order.side) << ' ' << order.userId << '\n';
        }
        output << "END_ORDERBOOK\n";
    }

    for (const auto& [userId, assets] : balances_) {
        for (const auto& [asset, bucket] : assets) {
            output << "BALANCE " << userId << ' ' << asset << ' ' << bucket.available << ' ' << bucket.locked << '\n';
        }
    }
    output << "END\n";
}

void Engine::loadSnapshot(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open snapshot file for reading");
    }

    orderbooks_.clear();
    balances_.clear();

    std::string line;
    Orderbook* currentOrderbook = nullptr;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line == "ENGINE_SNAPSHOT_V1") {
            continue;
        }

        std::istringstream stream(line);
        std::string tag;
        stream >> tag;
        if (tag == "ORDERBOOK") {
            std::string baseAsset;
            std::int64_t lastTradeId = 0;
            double currentPrice = 0.0;
            stream >> baseAsset >> lastTradeId >> currentPrice;
            orderbooks_.emplace_back(baseAsset, std::vector<Order>{}, std::vector<Order>{}, lastTradeId, currentPrice);
            currentOrderbook = &orderbooks_.back();
        } else if (tag == "BID" || tag == "ASK") {
            if (!currentOrderbook) {
                continue;
            }
            Order order;
            std::string side;
            stream >> order.price >> order.quantity >> order.orderId >> order.filled >> side >> order.userId;
            order.side = side == "buy" ? Side::Buy : Side::Sell;
            currentOrderbook->restoreOrder(order);
        } else if (tag == "END_ORDERBOOK") {
            currentOrderbook = nullptr;
        } else if (tag == "BALANCE") {
            std::string userId;
            std::string asset;
            BalanceBucket bucket;
            stream >> userId >> asset >> bucket.available >> bucket.locked;
            balances_[userId][asset] = bucket;
        } else if (tag == "END") {
            break;
        }
    }

    if (orderbooks_.empty()) {
        orderbooks_.emplace_back("TATA", std::vector<Order>{}, std::vector<Order>{}, 0, 0);
    }
    if (balances_.empty()) {
        setBaseBalances();
    }
}

const std::vector<Orderbook>& Engine::orderbooks() const {
    return orderbooks_;
}

const BalanceBook& Engine::balances() const {
    return balances_;
}

std::string Engine::generateOrderId() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    static constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::uniform_int_distribution<std::size_t> indexDist(0, sizeof(alphabet) - 2);

    std::string first;
    std::string second;
    first.reserve(13);
    second.reserve(13);
    for (int i = 0; i < 13; ++i) {
        first.push_back(alphabet[indexDist(rng)]);
        second.push_back(alphabet[indexDist(rng)]);
    }
    return first + second;
}

void Engine::handleCreateOrder(const CreateOrderRequest& request, const std::string& clientId) {
    try {
        const auto result = createOrder(request.market, request.price, request.quantity, request.side, request.userId);
        if (events_.sendToApi) {
            events_.sendToApi(clientId, ApiOrderPlacedResponse{result.orderId, result.executedQty, result.fills});
        }
    } catch (const std::exception&) {
        if (events_.sendToApi) {
            events_.sendToApi(clientId, ApiOrderCancelledResponse{"", 0.0, 0.0});
        }
    }
}

void Engine::handleCancelOrder(const CancelOrderRequest& request, const std::string& clientId) {
    try {
        auto* cancelOrderbook = findOrderbook(request.market);

        if (!cancelOrderbook) {
            throw std::runtime_error("No orderbook found");
        }

        const auto cancelledOrder = cancelOrderbook->cancelOrder(request.orderId);
        if (!cancelledOrder) {
            throw std::runtime_error("No order found");
        }

        const auto quoteAssetSeparator = request.market.find('_');
        const std::string quoteAsset = quoteAssetSeparator == std::string::npos ? std::string{} : request.market.substr(quoteAssetSeparator + 1);

        if (cancelledOrder->side == Side::Buy) {
            const double leftQuantity = (cancelledOrder->quantity - cancelledOrder->filled) * cancelledOrder->price;
            auto& userBalance = ensureUserBalance(cancelledOrder->userId);
            userBalance[BASE_CURRENCY].available += leftQuantity;
            userBalance[BASE_CURRENCY].locked -= leftQuantity;
        } else {
            const double leftQuantity = cancelledOrder->quantity - cancelledOrder->filled;
            auto& userBalance = ensureUserBalance(cancelledOrder->userId);
            userBalance[quoteAsset].available += leftQuantity;
            userBalance[quoteAsset].locked -= leftQuantity;
        }

        sendUpdatedDepthAt(std::to_string(cancelledOrder->price), request.market);

        if (events_.sendToApi) {
            events_.sendToApi(clientId, ApiOrderCancelledResponse{request.orderId, 0.0, 0.0});
        }
    } catch (const std::exception&) {
    }
}

void Engine::handleGetOpenOrders(const GetOpenOrdersRequest& request, const std::string& clientId) {
    try {
        const auto* openOrderbook = findOrderbook(request.market);
        if (!openOrderbook) {
            throw std::runtime_error("No orderbook found");
        }
        if (events_.sendToApi) {
            events_.sendToApi(clientId, ApiOpenOrdersResponse{openOrderbook->getOpenOrders(request.userId)});
        }
    } catch (const std::exception&) {
    }
}

void Engine::handleOnRamp(const OnRampRequest& request) {
    onRamp(request.userId, std::stod(request.amount));
}

void Engine::handleGetDepth(const GetDepthRequest& request, const std::string& clientId) {
    try {
        const auto* orderbook = findOrderbook(request.market);
        if (!orderbook) {
            throw std::runtime_error("No orderbook found");
        }
        if (events_.sendToApi) {
            events_.sendToApi(clientId, ApiDepthResponse{orderbook->getDepth()});
        }
    } catch (const std::exception&) {
        if (events_.sendToApi) {
            events_.sendToApi(clientId, ApiDepthResponse{Depth{}});
        }
    }
}

Engine::CreateOrderResult Engine::createOrder(const std::string& market, const std::string& price, const std::string& quantity, Side side, const std::string& userId) {
    auto* orderbook = findOrderbook(market);
    if (!orderbook) {
        throw std::runtime_error("No orderbook found");
    }

    const auto separator = market.find('_');
    const std::string baseAsset = separator == std::string::npos ? market : market.substr(0, separator);
    const std::string quoteAsset = separator == std::string::npos ? std::string{} : market.substr(separator + 1);

    checkAndLockFunds(baseAsset, quoteAsset, side, userId, price, quantity);

    Order order;
    order.price = std::stod(price);
    order.quantity = std::stod(quantity);
    order.orderId = generateOrderId();
    order.filled = 0.0;
    order.side = side;
    order.userId = userId;

    const auto result = orderbook->addOrder(order);
    updateBalance(userId, baseAsset, quoteAsset, side, result.fills, result.executedQty);
    createDbTrades(result.fills, market, userId);
    updateDbOrders(order, result.executedQty, result.fills, market);
    publishWsDepthUpdates(result.fills, price, side, market);
    publishWsTrades(result.fills, userId, market);

    return {result.executedQty, result.fills, order.orderId};
}

void Engine::updateDbOrders(const Order& order, double executedQty, const std::vector<Fill>& fills, const std::string& market) {
    if (events_.pushDbMessage) {
        events_.pushDbMessage(DbOrderUpdateMessage{order.orderId, executedQty, market, std::to_string(order.price), std::to_string(order.quantity), order.side});
        for (const auto& fill : fills) {
            events_.pushDbMessage(DbOrderUpdateMessage{fill.markerOrderId, fill.qty, std::nullopt, std::nullopt, std::nullopt, std::nullopt});
        }
    }
}

void Engine::createDbTrades(const std::vector<Fill>& fills, const std::string& market, const std::string& userId) {
    if (!events_.pushDbMessage) {
        return;
    }

    for (const auto& fill : fills) {
        events_.pushDbMessage(DbTradeAddedMessage{
            market,
            std::to_string(fill.tradeId),
            fill.otherUserId == userId,
            fill.price,
            std::to_string(fill.qty),
            std::to_string(fill.qty * std::stod(fill.price)),
            nowMs(),
        });
    }
}

void Engine::publishWsTrades(const std::vector<Fill>& fills, const std::string& userId, const std::string& market) {
    if (!events_.publishMessage) {
        return;
    }

    for (const auto& fill : fills) {
        events_.publishMessage("trade@" + market, WsTradeUpdateMessage{"trade@" + market, fill.tradeId, fill.otherUserId == userId, fill.price, std::to_string(fill.qty), market});
    }
}

void Engine::sendUpdatedDepthAt(const std::string& price, const std::string& market) {
    const auto* orderbook = findOrderbook(market);
    if (!orderbook || !events_.publishMessage) {
        return;
    }

    const auto depth = orderbook->getDepth();
    std::vector<std::pair<std::string, std::string>> updatedBids;
    std::vector<std::pair<std::string, std::string>> updatedAsks;

    for (const auto& level : depth.bids) {
        if (level.first == price) {
            updatedBids.push_back(level);
        }
    }
    for (const auto& level : depth.asks) {
        if (level.first == price) {
            updatedAsks.push_back(level);
        }
    }

    if (updatedBids.empty()) {
        updatedBids.push_back({price, "0"});
    }
    if (updatedAsks.empty()) {
        updatedAsks.push_back({price, "0"});
    }

    events_.publishMessage("depth@" + market, WsDepthUpdateMessage{"depth@" + market, updatedBids, updatedAsks});
}

void Engine::publishWsDepthUpdates(const std::vector<Fill>& fills, const std::string& price, Side side, const std::string& market) {
    const auto* orderbook = findOrderbook(market);
    if (!orderbook || !events_.publishMessage) {
        return;
    }

    const auto depth = orderbook->getDepth();
    if (side == Side::Buy) {
        std::vector<std::pair<std::string, std::string>> updatedAsks;
        std::vector<std::pair<std::string, std::string>> updatedBid;
        for (const auto& level : depth.asks) {
            const auto matched = std::any_of(fills.begin(), fills.end(), [&](const Fill& fill) {
                return fill.price == level.first;
            });
            if (matched) {
                updatedAsks.push_back(level);
            }
        }
        for (const auto& level : depth.bids) {
            if (level.first == price) {
                updatedBid.push_back(level);
                break;
            }
        }
        events_.publishMessage("depth@" + market, WsDepthUpdateMessage{"depth@" + market, updatedBid, updatedAsks});
        return;
    }

    std::vector<std::pair<std::string, std::string>> updatedBids;
    std::vector<std::pair<std::string, std::string>> updatedAsk;
    for (const auto& level : depth.bids) {
        const auto matched = std::any_of(fills.begin(), fills.end(), [&](const Fill& fill) {
            return fill.price == level.first;
        });
        if (matched) {
            updatedBids.push_back(level);
        }
    }
    for (const auto& level : depth.asks) {
        if (level.first == price) {
            updatedAsk.push_back(level);
            break;
        }
    }
    events_.publishMessage("depth@" + market, WsDepthUpdateMessage{"depth@" + market, updatedBids, updatedAsk});
}

void Engine::updateBalance(const std::string& userId, const std::string& baseAsset, const std::string& quoteAsset, Side side, const std::vector<Fill>& fills, double) {
    for (const auto& fill : fills) {
        const double fillPrice = std::stod(fill.price);
        if (side == Side::Buy) {
            ensureBalanceBucket(fill.otherUserId, quoteAsset).available += fill.qty * fillPrice;
            ensureBalanceBucket(userId, quoteAsset).locked -= fill.qty * fillPrice;
            ensureBalanceBucket(fill.otherUserId, baseAsset).locked -= fill.qty;
            ensureBalanceBucket(userId, baseAsset).available += fill.qty;
        } else {
            ensureBalanceBucket(fill.otherUserId, quoteAsset).locked -= fill.qty * fillPrice;
            ensureBalanceBucket(userId, quoteAsset).available += fill.qty * fillPrice;
            ensureBalanceBucket(fill.otherUserId, baseAsset).available += fill.qty;
            ensureBalanceBucket(userId, baseAsset).locked -= fill.qty;
        }
    }
}

void Engine::checkAndLockFunds(const std::string& baseAsset, const std::string& quoteAsset, Side side, const std::string& userId, const std::string& price, const std::string& quantity) {
    const double priceValue = std::stod(price);
    const double quantityValue = std::stod(quantity);

    if (side == Side::Buy) {
        auto& bucket = ensureBalanceBucket(userId, quoteAsset);
        const double cost = quantityValue * priceValue;
        if (bucket.available < cost) {
            throw std::runtime_error("Insufficient funds");
        }
        bucket.available -= cost;
        bucket.locked += cost;
        return;
    }

    auto& bucket = ensureBalanceBucket(userId, baseAsset);
    if (bucket.available < quantityValue) {
        throw std::runtime_error("Insufficient funds");
    }
    bucket.available -= quantityValue;
    bucket.locked += quantityValue;
}

void Engine::onRamp(const std::string& userId, double amount) {
    auto& userBalance = balances_[userId];
    if (userBalance.empty()) {
        userBalance[BASE_CURRENCY] = BalanceBucket{amount, 0.0};
        return;
    }
    userBalance[BASE_CURRENCY].available += amount;
}

void Engine::setBaseBalances() {
    balances_["1"][BASE_CURRENCY] = {10000000.0, 0.0};
    balances_["1"]["TATA"] = {10000000.0, 0.0};

    balances_["2"][BASE_CURRENCY] = {10000000.0, 0.0};
    balances_["2"]["TATA"] = {10000000.0, 0.0};

    balances_["5"][BASE_CURRENCY] = {10000000.0, 0.0};
    balances_["5"]["TATA"] = {10000000.0, 0.0};
}

Orderbook* Engine::findOrderbook(const std::string& market) {
    const auto it = std::find_if(orderbooks_.begin(), orderbooks_.end(), [&](const Orderbook& orderbook) {
        return orderbook.ticker() == market;
    });
    if (it == orderbooks_.end()) {
        return nullptr;
    }
    return &*it;
}

const Orderbook* Engine::findOrderbook(const std::string& market) const {
    const auto it = std::find_if(orderbooks_.begin(), orderbooks_.end(), [&](const Orderbook& orderbook) {
        return orderbook.ticker() == market;
    });
    if (it == orderbooks_.end()) {
        return nullptr;
    }
    return &*it;
}

UserBalance& Engine::ensureUserBalance(const std::string& userId) {
    return balances_[userId];
}

BalanceBucket& Engine::ensureBalanceBucket(const std::string& userId, const std::string& asset) {
    return balances_[userId][asset];
}

} // namespace whiskers
