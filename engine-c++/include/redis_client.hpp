#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace whiskers {

class RedisClient {
public:
    RedisClient(std::string host = "127.0.0.1", std::uint16_t port = 6379);
    ~RedisClient();

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    RedisClient(RedisClient&& other) noexcept;
    RedisClient& operator=(RedisClient&& other) noexcept;

    std::optional<std::string> blockingPop(const std::string& key, int timeoutSeconds = 0);
    void pushLeft(const std::string& key, const std::string& value);
    void publish(const std::string& channel, const std::string& value);

private:
    std::string host_;
    std::uint16_t port_;
    int socketFd_ = -1;

    void connectSocket();
    void closeSocket();
    void ensureConnected();
    void sendCommand(const std::vector<std::string>& parts);
    std::string readLine();
    std::optional<std::vector<std::string>> readArrayReply();
    void readIntegerReply();
    void sendAll(const std::string& data);
};

} // namespace whiskers
