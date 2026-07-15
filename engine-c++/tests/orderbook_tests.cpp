#include "orderbook.hpp"

#include <cassert>

namespace {

whiskers::Order makeOrder(double price, double quantity, const std::string& orderId, whiskers::Side side, const std::string& userId) {
    return whiskers::Order{price, quantity, orderId, 0.0, side, userId};
}

} // namespace

void runOrderbookTests() {
    using namespace whiskers;

    {
        Orderbook orderbook("TATA", {}, {}, 0, 0);
        const auto result = orderbook.addOrder(makeOrder(1000.0, 1.0, "1", Side::Buy, "1"));
        assert(result.fills.empty());
        assert(result.executedQty == 0.0);
        assert(orderbook.bids().size() == 1);
    }

    {
        Orderbook orderbook("TATA", {makeOrder(1000.0, 1.0, "1", Side::Buy, "1")}, {}, 0, 0);
        const auto result = orderbook.addOrder(makeOrder(1000.0, 2.0, "2", Side::Sell, "2"));
        assert(result.fills.size() == 1);
        assert(result.executedQty == 1.0);
    }

    {
        Orderbook orderbook(
            "TATA",
            {makeOrder(999.0, 1.0, "1", Side::Buy, "1")},
            {makeOrder(1001.0, 1.0, "2", Side::Sell, "2")},
            0,
            0
        );

        const auto result = orderbook.addOrder(makeOrder(1001.0, 2.0, "3", Side::Buy, "3"));
        assert(result.fills.size() == 1);
        assert(result.executedQty == 1.0);
        assert(orderbook.bids().size() == 2);
        assert(orderbook.asks().empty());
    }

    {
        Orderbook orderbook(
            "TATA",
            {makeOrder(1000.0, 2.0, "1", Side::Buy, "1")},
            {makeOrder(1002.0, 3.0, "2", Side::Sell, "2")},
            0,
            0
        );
        const auto depth = orderbook.getDepth();
        assert(depth.bids.size() == 1);
        assert(depth.asks.size() == 1);
    }

}
