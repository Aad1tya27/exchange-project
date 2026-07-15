#include "engine.hpp"

#include <cassert>
#include <variant>

namespace {

using namespace whiskers;

struct CapturedEvents {
    std::vector<std::pair<std::string, ApiResponse>> apiResponses;
    std::vector<DbMessage> dbMessages;
    std::vector<std::pair<std::string, WsMessage>> wsMessages;
};

} // namespace

void runOrderbookTests();

static void runEngineTests() {
    CapturedEvents captured;
    EngineEvents events;
    events.sendToApi = [&](const std::string& clientId, const ApiResponse& response) {
        captured.apiResponses.emplace_back(clientId, response);
    };
    events.pushDbMessage = [&](const DbMessage& message) {
        captured.dbMessages.push_back(message);
    };
    events.publishMessage = [&](const std::string& channel, const WsMessage& message) {
        captured.wsMessages.emplace_back(channel, message);
    };

    Engine engine(events);

    engine.process(CreateOrderRequest{"TATA_INR", "1000", "1", Side::Buy, "1"}, "client-1");
    engine.process(CreateOrderRequest{"TATA_INR", "1001", "1", Side::Sell, "2"}, "client-1");

    assert(captured.wsMessages.size() >= 2);
    assert(captured.apiResponses.size() == 2);
    assert(std::holds_alternative<ApiOrderPlacedResponse>(captured.apiResponses.front().second));
}

int main() {
    runOrderbookTests();
    runEngineTests();
    return 0;
}
