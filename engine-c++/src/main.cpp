#include "engine.hpp"
#include "redis_client.hpp"
#include "redis_protocol.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

constexpr const char* DEFAULT_REDIS_HOST = "127.0.0.1";
constexpr std::uint16_t DEFAULT_REDIS_PORT = 6379;

std::string readEnv(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return fallback;
    }
    return value;
}

std::uint16_t readPort(const char* name, std::uint16_t fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return fallback;
    }
    return static_cast<std::uint16_t>(std::stoi(value));
}

} // namespace

int main() {
    const auto redisHost = readEnv("REDIS_HOST", DEFAULT_REDIS_HOST);
    const auto redisPort = readPort("REDIS_PORT", DEFAULT_REDIS_PORT);

    auto publisher = std::make_shared<whiskers::RedisClient>(redisHost, redisPort);
    whiskers::EngineEvents events;
    events.sendToApi = [publisher](const std::string& clientId, const whiskers::ApiResponse& response) {
        publisher->publish(clientId, whiskers::serializeApiResponse(response));
    };
    events.pushDbMessage = [publisher](const whiskers::DbMessage& message) {
        publisher->pushLeft("db_processor", whiskers::serializeDbMessage(message));
    };
    events.publishMessage = [publisher](const std::string& channel, const whiskers::WsMessage& message) {
        publisher->publish(channel, whiskers::serializeWsMessage(message));
    };

    whiskers::Engine engine(events);
    std::cout << "connected to redis" << std::endl;

    while (true) {
        try {
            whiskers::RedisClient consumer(redisHost, redisPort);
            while (true) {
                const auto response = consumer.blockingPop("messages");
                if (!response) {
                    continue;
                }

                const auto envelope = whiskers::parseIncomingEnvelope(*response);
                if (!envelope) {
                    continue;
                }

                engine.process(envelope->message, envelope->clientId);
            }
        } catch (const std::exception& error) {
            std::cerr << "engine redis loop error: " << error.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}
