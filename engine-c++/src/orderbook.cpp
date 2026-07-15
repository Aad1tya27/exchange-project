#include "orderbook.hpp"

#include <algorithm>
#include <iterator>
#include <map>

namespace whiskers {

Orderbook::Orderbook(std::string baseAsset, std::vector<Order> bids, std::vector<Order> asks, std::int64_t lastTradeId, double currentPrice)
    : baseAsset_(std::move(baseAsset)), lastTradeId_(lastTradeId), currentPrice_(currentPrice) {
    for (auto& order : bids) {
        storeOrder(std::move(order), true);
    }
    for (auto& order : asks) {
        storeOrder(std::move(order), false);
    }
}

const std::string& Orderbook::baseAsset() const {
    return baseAsset_;
}

std::string Orderbook::ticker() const {
    return baseAsset_ + "_" + quoteAsset_;
}

OrderbookSnapshot Orderbook::getSnapshot() const {
    std::vector<Order> bidSnapshot;
    std::vector<Order> askSnapshot;

    for (const auto& [price, bucket] : bids_) {
        (void)price;
        for (const auto& order : bucket.orders) {
            bidSnapshot.push_back(order);
        }
    }

    for (const auto& [price, bucket] : asks_) {
        (void)price;
        for (const auto& order : bucket.orders) {
            askSnapshot.push_back(order);
        }
    }

    return {baseAsset_, std::move(bidSnapshot), std::move(askSnapshot), lastTradeId_, currentPrice_};
}

MatchResult Orderbook::addOrder(Order order) {
    if (order.side == Side::Buy) {
        MatchResult result = matchBid(order);
        order.filled = result.executedQty;
        if (result.executedQty < order.quantity) {
            storeOrder(std::move(order), true);
        }
        return result;
    }

    MatchResult result = matchAsk(order);
    order.filled = result.executedQty;
    if (result.executedQty < order.quantity) {
        storeOrder(std::move(order), false);
    }
    return result;
}

MatchResult Orderbook::matchBid(Order order) {
    MatchResult result;
    for (auto priceIt = asks_.begin(); priceIt != asks_.end() && result.executedQty < order.quantity;) {
        if (priceIt->first > order.price) {
            break;
        }

        auto& bucket = priceIt->second.orders;
        for (auto it = bucket.begin(); it != bucket.end() && result.executedQty < order.quantity;) {
            const double filledQty = std::min(order.quantity - result.executedQty, it->quantity - it->filled);
            if (filledQty <= 0) {
                ++it;
                continue;
            }
            result.executedQty += filledQty;
            it->filled += filledQty;
            result.fills.push_back({std::to_string(it->price), filledQty, lastTradeId_++, it->userId, it->orderId});
            currentPrice_ = it->price;

            if (it->filled >= it->quantity) {
                eraseIndexedOrder(it->orderId);
                it = bucket.erase(it);
            } else {
                ++it;
            }
        }

        if (bucket.empty()) {
            priceIt = asks_.erase(priceIt);
        } else {
            ++priceIt;
        }
    }

    return result;
}

MatchResult Orderbook::matchAsk(Order order) {
    MatchResult result;
    for (auto priceIt = bids_.begin(); priceIt != bids_.end() && result.executedQty < order.quantity;) {
        if (priceIt->first < order.price) {
            break;
        }

        auto& bucket = priceIt->second.orders;
        for (auto it = bucket.begin(); it != bucket.end() && result.executedQty < order.quantity;) {
            const double filledQty = std::min(order.quantity - result.executedQty, it->quantity - it->filled);
            if (filledQty <= 0) {
                ++it;
                continue;
            }
            result.executedQty += filledQty;
            it->filled += filledQty;
            result.fills.push_back({std::to_string(it->price), filledQty, lastTradeId_++, it->userId, it->orderId});
            currentPrice_ = it->price;

            if (it->filled >= it->quantity) {
                eraseIndexedOrder(it->orderId);
                it = bucket.erase(it);
            } else {
                ++it;
            }
        }

        if (bucket.empty()) {
            priceIt = bids_.erase(priceIt);
        } else {
            ++priceIt;
        }
    }

    return result;
}

void Orderbook::restoreOrder(Order order) {
    storeOrder(std::move(order), order.side == Side::Buy);
}

Depth Orderbook::getDepth() const {
    Depth depth;
    for (const auto& [price, bucket] : bids_) {
        double total = 0.0;
        for (const auto& order : bucket.orders) {
            total += order.quantity - order.filled;
        }
        depth.bids.push_back({std::to_string(price), std::to_string(total)});
    }
    for (const auto& [price, bucket] : asks_) {
        double total = 0.0;
        for (const auto& order : bucket.orders) {
            total += order.quantity - order.filled;
        }
        depth.asks.push_back({std::to_string(price), std::to_string(total)});
    }

    return depth;
}

std::vector<Order> Orderbook::getOpenOrders(const std::string& userId) const {
    std::vector<Order> openOrders;
    for (const auto& [price, bucket] : asks_) {
        (void)price;
        for (const auto& openOrder : bucket.orders) {
            if (openOrder.userId == userId) {
                openOrders.push_back(openOrder);
            }
        }
    }
    for (const auto& [price, bucket] : bids_) {
        (void)price;
        for (const auto& openOrder : bucket.orders) {
            if (openOrder.userId == userId) {
                openOrders.push_back(openOrder);
            }
        }
    }
    return openOrders;
}

std::optional<Order> Orderbook::cancelOrder(const std::string& orderId) {
    auto indexIt = orderIndex_.find(orderId);
    if (indexIt == orderIndex_.end()) {
        return std::nullopt;
    }

    Order order = *(indexIt->second.iterator);
    if (indexIt->second.isBid) {
        auto priceIt = bids_.find(indexIt->second.price);
        if (priceIt != bids_.end()) {
            auto& bucket = priceIt->second.orders;
            bucket.erase(indexIt->second.iterator);
            if (bucket.empty()) {
                bids_.erase(priceIt);
            }
        }
    } else {
        auto priceIt = asks_.find(indexIt->second.price);
        if (priceIt != asks_.end()) {
            auto& bucket = priceIt->second.orders;
            bucket.erase(indexIt->second.iterator);
            if (bucket.empty()) {
                asks_.erase(priceIt);
            }
        }
    }
    orderIndex_.erase(indexIt);
    return order;
}

const Orderbook::BidBook& Orderbook::bids() const {
    return bids_;
}

const Orderbook::AskBook& Orderbook::asks() const {
    return asks_;
}

void Orderbook::rebuildIndex() {
    orderIndex_.clear();
    for (auto& [price, bucket] : bids_) {
        for (auto it = bucket.orders.begin(); it != bucket.orders.end(); ++it) {
            orderIndex_.emplace(it->orderId, IndexedOrder{true, price, it});
        }
    }
    for (auto& [price, bucket] : asks_) {
        for (auto it = bucket.orders.begin(); it != bucket.orders.end(); ++it) {
            orderIndex_.emplace(it->orderId, IndexedOrder{false, price, it});
        }
    }
}

void Orderbook::storeOrder(Order order, bool isBid) {
    auto& bucket = isBid ? bids_[order.price] : asks_[order.price];
    bucket.orders.push_back(std::move(order));
    auto it = std::prev(bucket.orders.end());
    orderIndex_[it->orderId] = IndexedOrder{isBid, it->price, it};
}

void Orderbook::eraseIndexedOrder(const std::string& orderId) {
    orderIndex_.erase(orderId);
}

std::optional<Order> Orderbook::eraseOrder(const std::string& orderId) {
    return cancelOrder(orderId);
}

void Orderbook::eraseIfBucketEmpty(bool isBid, double price) {
    if (isBid) {
        auto it = bids_.find(price);
        if (it != bids_.end() && it->second.orders.empty()) {
            bids_.erase(it);
        }
        return;
    }

    auto it = asks_.find(price);
    if (it != asks_.end() && it->second.orders.empty()) {
        asks_.erase(it);
    }
}

} // namespace whiskers
