#pragma once

#include "orderbook.hpp"

#include <functional>
#include <optional>
#include <string>

namespace whiskers {

class Engine {
public:
    explicit Engine(const EngineEvents& events = {});

    void process(const ApiMessage& message, const std::string& clientId);
    void addOrderbook(Orderbook orderbook);
    void saveSnapshot(const std::string& path = "snapshot.txt") const;
    void loadSnapshot(const std::string& path = "snapshot.txt");

    const std::vector<Orderbook>& orderbooks() const;
    const BalanceBook& balances() const;

    static std::string generateOrderId();

private:
    struct CreateOrderResult {
        double executedQty = 0.0;
        std::vector<Fill> fills;
        std::string orderId;
    };

    void handleCreateOrder(const CreateOrderRequest& request, const std::string& clientId);
    void handleCancelOrder(const CancelOrderRequest& request, const std::string& clientId);
    void handleGetOpenOrders(const GetOpenOrdersRequest& request, const std::string& clientId);
    void handleOnRamp(const OnRampRequest& request);
    void handleGetDepth(const GetDepthRequest& request, const std::string& clientId);

    CreateOrderResult createOrder(const std::string& market, const std::string& price, const std::string& quantity, Side side, const std::string& userId);
    void updateDbOrders(const Order& order, double executedQty, const std::vector<Fill>& fills, const std::string& market);
    void createDbTrades(const std::vector<Fill>& fills, const std::string& market, const std::string& userId);
    void publishWsTrades(const std::vector<Fill>& fills, const std::string& userId, const std::string& market);
    void sendUpdatedDepthAt(const std::string& price, const std::string& market);
    void publishWsDepthUpdates(const std::vector<Fill>& fills, const std::string& price, Side side, const std::string& market);
    void updateBalance(const std::string& userId, const std::string& baseAsset, const std::string& quoteAsset, Side side, const std::vector<Fill>& fills, double executedQty);
    void checkAndLockFunds(const std::string& baseAsset, const std::string& quoteAsset, Side side, const std::string& userId, const std::string& price, const std::string& quantity);
    void onRamp(const std::string& userId, double amount);
    void setBaseBalances();
    Orderbook* findOrderbook(const std::string& market);
    const Orderbook* findOrderbook(const std::string& market) const;
    UserBalance& ensureUserBalance(const std::string& userId);
    BalanceBucket& ensureBalanceBucket(const std::string& userId, const std::string& asset);

    EngineEvents events_;
    std::vector<Orderbook> orderbooks_;
    BalanceBook balances_;
};

} // namespace whiskers
