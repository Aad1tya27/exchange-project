#include "redis_client.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>

namespace whiskers {

namespace {

std::runtime_error redisError(const std::string& message) {
    return std::runtime_error("Redis client error: " + message);
}

} // namespace

RedisClient::RedisClient(std::string host, std::uint16_t port)
    : host_(std::move(host)), port_(port) {
    connectSocket();
}

RedisClient::~RedisClient() {
    closeSocket();
}

RedisClient::RedisClient(RedisClient&& other) noexcept
    : host_(std::move(other.host_)), port_(other.port_), socketFd_(other.socketFd_) {
    other.socketFd_ = -1;
}

RedisClient& RedisClient::operator=(RedisClient&& other) noexcept {
    if (this != &other) {
        closeSocket();
        host_ = std::move(other.host_);
        port_ = other.port_;
        socketFd_ = other.socketFd_;
        other.socketFd_ = -1;
    }
    return *this;
}

void RedisClient::closeSocket() {
    if (socketFd_ != -1) {
        ::close(socketFd_);
        socketFd_ = -1;
    }
}

void RedisClient::connectSocket() {
    closeSocket();

    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* result = nullptr;
    const auto portString = std::to_string(port_);
    const int status = ::getaddrinfo(host_.c_str(), portString.c_str(), &hints, &result);
    if (status != 0) {
        throw redisError(::gai_strerror(status));
    }

    for (auto* entry = result; entry != nullptr; entry = entry->ai_next) {
        const int fd = ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
        if (fd == -1) {
            continue;
        }

        if (::connect(fd, entry->ai_addr, entry->ai_addrlen) == 0) {
            socketFd_ = fd;
            break;
        }

        ::close(fd);
    }

    ::freeaddrinfo(result);

    if (socketFd_ == -1) {
        throw redisError("Unable to connect to Redis");
    }
}

void RedisClient::ensureConnected() {
    if (socketFd_ == -1) {
        connectSocket();
    }
}

void RedisClient::sendAll(const std::string& data) {
    const char* buffer = data.data();
    std::size_t remaining = data.size();

    while (remaining > 0) {
        const ssize_t sent = ::send(socketFd_, buffer, remaining, 0);
        if (sent <= 0) {
            throw redisError("Failed to send command");
        }
        buffer += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
}

void RedisClient::sendCommand(const std::vector<std::string>& parts) {
    ensureConnected();

    std::string command;
    command.reserve(64);
    command += "*" + std::to_string(parts.size()) + "\r\n";
    for (const auto& part : parts) {
        command += "$" + std::to_string(part.size()) + "\r\n" + part + "\r\n";
    }

    sendAll(command);
}

std::string RedisClient::readLine() {
    std::string line;
    char ch = 0;
    char previous = 0;

    while (true) {
        const ssize_t received = ::recv(socketFd_, &ch, 1, 0);
        if (received <= 0) {
            throw redisError("Failed to read reply");
        }

        if (previous == '\r' && ch == '\n') {
            if (!line.empty()) {
                line.pop_back();
            }
            return line;
        }

        line.push_back(ch);
        previous = ch;
    }
}

void RedisClient::readIntegerReply() {
    ensureConnected();
    const std::string line = readLine();
    if (line.empty()) {
        throw redisError("Empty Redis reply");
    }
    if (line[0] != ':' && line[0] != '+') {
        throw redisError("Unexpected Redis reply type");
    }
}

std::optional<std::vector<std::string>> RedisClient::readArrayReply() {
    ensureConnected();

    char prefix = 0;
    if (::recv(socketFd_, &prefix, 1, 0) <= 0) {
        throw redisError("Failed to read array reply");
    }
    if (prefix != '*') {
        throw redisError("Expected array reply");
    }

    const int count = std::stoi(readLine());
    std::vector<std::string> values;
    values.reserve(static_cast<std::size_t>(count));

    for (int index = 0; index < count; ++index) {
        char bulkPrefix = 0;
        if (::recv(socketFd_, &bulkPrefix, 1, 0) <= 0 || bulkPrefix != '$') {
            throw redisError("Expected bulk string reply");
        }

        const int length = std::stoi(readLine());
        if (length < 0) {
            values.emplace_back();
            continue;
        }

        std::string value(static_cast<std::size_t>(length), '\0');
        std::size_t readBytes = 0;
        while (readBytes < static_cast<std::size_t>(length)) {
            const ssize_t received = ::recv(socketFd_, value.data() + readBytes, static_cast<std::size_t>(length) - readBytes, 0);
            if (received <= 0) {
                throw redisError("Failed to read bulk string");
            }
            readBytes += static_cast<std::size_t>(received);
        }

        char crlf[2] = {};
        if (::recv(socketFd_, crlf, 2, 0) != 2) {
            throw redisError("Failed to read CRLF");
        }

        values.push_back(std::move(value));
    }

    return values;
}

std::optional<std::string> RedisClient::blockingPop(const std::string& key, int timeoutSeconds) {
    try {
        sendCommand({"BRPOP", key, std::to_string(timeoutSeconds)});
        const auto reply = readArrayReply();
        if (!reply || reply->size() < 2) {
            return std::nullopt;
        }
        return reply->at(1);
    } catch (...) {
        closeSocket();
        throw;
    }
}

void RedisClient::pushLeft(const std::string& key, const std::string& value) {
    try {
        sendCommand({"LPUSH", key, value});
        readIntegerReply();
    } catch (...) {
        closeSocket();
        throw;
    }
}

void RedisClient::publish(const std::string& channel, const std::string& value) {
    try {
        sendCommand({"PUBLISH", channel, value});
        readIntegerReply();
    } catch (...) {
        closeSocket();
        throw;
    }
}

} // namespace whiskers
