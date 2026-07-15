#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace whiskers {

inline constexpr const char* BASE_CURRENCY = "INR";

enum class Side {
    Buy,
    Sell,
};

inline std::string toString(Side side) {
    return side == Side::Buy ? "buy" : "sell";
}

inline std::optional<Side> sideFromString(const std::string& value) {
    if (value == "buy") {
        return Side::Buy;
    }
    if (value == "sell") {
        return Side::Sell;
    }
    return std::nullopt;
}

struct Order {
    double price = 0.0;
    double quantity = 0.0;
    std::string orderId;
    double filled = 0.0;
    Side side = Side::Buy;
    std::string userId;
};

struct Fill {
    std::string price;
    double qty = 0.0;
    std::int64_t tradeId = 0;
    std::string otherUserId;
    std::string markerOrderId;
};

struct Depth {
    std::vector<std::pair<std::string, std::string>> bids;
    std::vector<std::pair<std::string, std::string>> asks;
};

struct OrderbookSnapshot {
    std::string baseAsset;
    std::vector<Order> bids;
    std::vector<Order> asks;
    std::int64_t lastTradeId = 0;
    double currentPrice = 0.0;
};

struct BalanceBucket {
    double available = 0.0;
    double locked = 0.0;
};

using UserBalance = std::unordered_map<std::string, BalanceBucket>;
using BalanceBook = std::unordered_map<std::string, UserBalance>;

struct CreateOrderRequest {
    std::string market;
    std::string price;
    std::string quantity;
    Side side = Side::Buy;
    std::string userId;
};

struct CancelOrderRequest {
    std::string orderId;
    std::string market;
};

struct OnRampRequest {
    std::string amount;
    std::string userId;
    std::string txnId;
};

struct GetDepthRequest {
    std::string market;
};

struct GetOpenOrdersRequest {
    std::string userId;
    std::string market;
};

using ApiMessage = std::variant<CreateOrderRequest, CancelOrderRequest, OnRampRequest, GetDepthRequest, GetOpenOrdersRequest>;

struct ApiDepthResponse {
    Depth payload;
};

struct ApiOrderPlacedResponse {
    std::string orderId;
    double executedQty = 0.0;
    std::vector<Fill> fills;
};

struct ApiOrderCancelledResponse {
    std::string orderId;
    double executedQty = 0.0;
    double remainingQty = 0.0;
};

struct ApiOpenOrdersResponse {
    std::vector<Order> orders;
};

using ApiResponse = std::variant<ApiDepthResponse, ApiOrderPlacedResponse, ApiOrderCancelledResponse, ApiOpenOrdersResponse>;

struct DbTradeAddedMessage {
    std::string market;
    std::string id;
    bool isBuyerMaker = false;
    std::string price;
    std::string quantity;
    std::string quoteQuantity;
    std::int64_t timestamp = 0;
};

struct DbOrderUpdateMessage {
    std::string orderId;
    double executedQty = 0.0;
    std::optional<std::string> market;
    std::optional<std::string> price;
    std::optional<std::string> quantity;
    std::optional<Side> side;
};

using DbMessage = std::variant<DbTradeAddedMessage, DbOrderUpdateMessage>;

struct WsDepthUpdateMessage {
    std::string stream;
    std::vector<std::pair<std::string, std::string>> bids;
    std::vector<std::pair<std::string, std::string>> asks;
};

struct WsTradeUpdateMessage {
    std::string stream;
    std::int64_t tradeId = 0;
    bool marketSide = false;
    std::string price;
    std::string quantity;
    std::string symbol;
};

using WsMessage = std::variant<WsDepthUpdateMessage, WsTradeUpdateMessage>;

struct EngineEvents {
    std::function<void(const std::string&, const ApiResponse&)> sendToApi;
    std::function<void(const DbMessage&)> pushDbMessage;
    std::function<void(const std::string&, const WsMessage&)> publishMessage;
};

} // namespace whiskers
