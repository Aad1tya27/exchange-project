#pragma once

#include "types.hpp"

#include <map>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>

namespace whiskers {

struct MatchResult {
    double executedQty = 0.0;
    std::vector<Fill> fills;
};

class Orderbook {
private:
    using OrderList = std::list<Order>;
    using OrderIterator = OrderList::iterator;

    struct PriceBucket {
        OrderList orders;
    };

    using BidBook = std::map<double, PriceBucket, std::greater<double>>;
    using AskBook = std::map<double, PriceBucket>;

public:
    Orderbook(std::string baseAsset, std::vector<Order> bids, std::vector<Order> asks, std::int64_t lastTradeId, double currentPrice);

    const std::string& baseAsset() const;
    std::string ticker() const;

    OrderbookSnapshot getSnapshot() const;
    MatchResult addOrder(Order order);
    MatchResult matchBid(Order order);
    MatchResult matchAsk(Order order);
    void restoreOrder(Order order);
    Depth getDepth() const;
    std::vector<Order> getOpenOrders(const std::string& userId) const;
    std::optional<Order> cancelOrder(const std::string& orderId);

    const BidBook& bids() const;
    const AskBook& asks() const;

private:
    struct IndexedOrder {
        bool isBid = false;
        double price = 0.0;
        OrderIterator iterator;
    };

    void rebuildIndex();
    void storeOrder(Order order, bool isBid);
    void eraseIndexedOrder(const std::string& orderId);
    std::optional<Order> eraseOrder(const std::string& orderId);
    void eraseIfBucketEmpty(bool isBid, double price);

    BidBook bids_;
    AskBook asks_;
    std::unordered_map<std::string, IndexedOrder> orderIndex_;
    std::string baseAsset_;
    std::string quoteAsset_ = BASE_CURRENCY;
    std::int64_t lastTradeId_ = 0;
    double currentPrice_ = 0.0;
};

} // namespace whiskers
