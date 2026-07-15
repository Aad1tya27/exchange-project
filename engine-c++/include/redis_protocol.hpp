#pragma once

#include "types.hpp"

#include <optional>
#include <string>

namespace whiskers {

struct IncomingEnvelope {
    std::string clientId;
    ApiMessage message;
};

std::optional<IncomingEnvelope> parseIncomingEnvelope(const std::string& payload);
std::string serializeApiResponse(const ApiResponse& response);
std::string serializeDbMessage(const DbMessage& message);
std::string serializeWsMessage(const WsMessage& message);

} // namespace whiskers
