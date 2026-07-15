#include "redis_protocol.hpp"

#include <cctype>
#include <sstream>
#include <stdexcept>

namespace whiskers {

namespace {

std::string escapeJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

std::string joinDepthLevels(const std::vector<std::pair<std::string, std::string>>& levels) {
    std::ostringstream output;
    output << '[';
    for (std::size_t index = 0; index < levels.size(); ++index) {
        if (index > 0) {
            output << ',';
        }
        output << "[\"" << escapeJson(levels[index].first) << "\",\"" << escapeJson(levels[index].second) << "\"]";
    }
    output << ']';
    return output.str();
}

std::string joinOrders(const std::vector<Order>& orders) {
    std::ostringstream output;
    output << '[';
    for (std::size_t index = 0; index < orders.size(); ++index) {
        if (index > 0) {
            output << ',';
        }
        const auto& order = orders[index];
        output << '{'
               << "\"price\":" << order.price << ','
               << "\"quantity\":" << order.quantity << ','
               << "\"orderId\":\"" << escapeJson(order.orderId) << "\"," 
               << "\"filled\":" << order.filled << ','
               << "\"side\":\"" << toString(order.side) << "\"," 
               << "\"userId\":\"" << escapeJson(order.userId) << "\"}";
    }
    output << ']';
    return output.str();
}

std::optional<std::string> extractStringField(const std::string& json, const std::string& key) {
    const std::string pattern = "\"" + key + "\"";
    const auto keyPos = json.find(pattern);
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }

    auto valuePos = json.find(':', keyPos + pattern.size());
    if (valuePos == std::string::npos) {
        return std::nullopt;
    }
    valuePos = json.find('"', valuePos);
    if (valuePos == std::string::npos) {
        return std::nullopt;
    }
    ++valuePos;

    std::string value;
    for (std::size_t index = valuePos; index < json.size(); ++index) {
        const char ch = json[index];
        if (ch == '\\' && index + 1 < json.size()) {
            value.push_back(json[index + 1]);
            ++index;
            continue;
        }
        if (ch == '"') {
            return value;
        }
        value.push_back(ch);
    }

    return std::nullopt;
}

std::optional<double> extractNumberField(const std::string& json, const std::string& key) {
    const std::string pattern = "\"" + key + "\"";
    const auto keyPos = json.find(pattern);
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }

    auto valuePos = json.find(':', keyPos + pattern.size());
    if (valuePos == std::string::npos) {
        return std::nullopt;
    }
    ++valuePos;
    while (valuePos < json.size() && std::isspace(static_cast<unsigned char>(json[valuePos]))) {
        ++valuePos;
    }

    std::size_t endPos = valuePos;
    while (endPos < json.size()) {
        const char ch = json[endPos];
        if (!(std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' || ch == '-' || ch == '+')) {
            break;
        }
        ++endPos;
    }

    if (endPos == valuePos) {
        return std::nullopt;
    }

    return std::stod(json.substr(valuePos, endPos - valuePos));
}

std::optional<Side> extractSideField(const std::string& json) {
    const auto side = extractStringField(json, "side");
    if (!side) {
        return std::nullopt;
    }
    return sideFromString(*side);
}

std::string serializeFillArray(const std::vector<Fill>& fills) {
    std::ostringstream output;
    output << '[';
    for (std::size_t index = 0; index < fills.size(); ++index) {
        if (index > 0) {
            output << ',';
        }
        const auto& fill = fills[index];
        output << '{'
               << "\"price\":\"" << escapeJson(fill.price) << "\"," 
               << "\"qty\":" << fill.qty << ','
               << "\"tradeId\":" << fill.tradeId
               << '}';
    }
    output << ']';
    return output.str();
}

} // namespace

std::optional<IncomingEnvelope> parseIncomingEnvelope(const std::string& payload) {
    const auto clientId = extractStringField(payload, "clientId");
    const auto type = extractStringField(payload, "type");
    if (!clientId || !type) {
        return std::nullopt;
    }

    if (*type == "CREATE_ORDER") {
        const auto market = extractStringField(payload, "market");
        const auto price = extractStringField(payload, "price");
        const auto quantity = extractStringField(payload, "quantity");
        const auto side = extractSideField(payload);
        const auto userId = extractStringField(payload, "userId");
        if (!market || !price || !quantity || !side || !userId) {
            return std::nullopt;
        }
        return IncomingEnvelope{*clientId, CreateOrderRequest{*market, *price, *quantity, *side, *userId}};
    }

    if (*type == "CANCEL_ORDER") {
        const auto orderId = extractStringField(payload, "orderId");
        const auto market = extractStringField(payload, "market");
        if (!orderId || !market) {
            return std::nullopt;
        }
        return IncomingEnvelope{*clientId, CancelOrderRequest{*orderId, *market}};
    }

    if (*type == "ON_RAMP") {
        const auto amount = extractStringField(payload, "amount");
        const auto userId = extractStringField(payload, "userId");
        const auto txnId = extractStringField(payload, "txnId");
        if (!amount || !userId || !txnId) {
            return std::nullopt;
        }
        return IncomingEnvelope{*clientId, OnRampRequest{*amount, *userId, *txnId}};
    }

    if (*type == "GET_DEPTH") {
        const auto market = extractStringField(payload, "market");
        if (!market) {
            return std::nullopt;
        }
        return IncomingEnvelope{*clientId, GetDepthRequest{*market}};
    }

    if (*type == "GET_OPEN_ORDERS") {
        const auto userId = extractStringField(payload, "userId");
        const auto market = extractStringField(payload, "market");
        if (!userId || !market) {
            return std::nullopt;
        }
        return IncomingEnvelope{*clientId, GetOpenOrdersRequest{*userId, *market}};
    }

    return std::nullopt;
}

std::string serializeApiResponse(const ApiResponse& response) {
    return std::visit([](const auto& value) -> std::string {
        using T = std::decay_t<decltype(value)>;
        std::ostringstream output;

        if constexpr (std::is_same_v<T, ApiDepthResponse>) {
            output << '{'
                   << "\"type\":\"DEPTH\"," 
                   << "\"payload\":{"
                   << "\"bids\":" << joinDepthLevels(value.payload.bids) << ','
                   << "\"asks\":" << joinDepthLevels(value.payload.asks)
                   << "}}";
        } else if constexpr (std::is_same_v<T, ApiOrderPlacedResponse>) {
            output << '{'
                   << "\"type\":\"ORDER_PLACED\"," 
                   << "\"payload\":{"
                   << "\"orderId\":\"" << escapeJson(value.orderId) << "\"," 
                   << "\"executedQty\":" << value.executedQty << ','
                   << "\"fills\":" << serializeFillArray(value.fills)
                   << "}}";
        } else if constexpr (std::is_same_v<T, ApiOrderCancelledResponse>) {
            output << '{'
                   << "\"type\":\"ORDER_CANCELLED\"," 
                   << "\"payload\":{"
                   << "\"orderId\":\"" << escapeJson(value.orderId) << "\"," 
                   << "\"executedQty\":" << value.executedQty << ','
                   << "\"remainingQty\":" << value.remainingQty
                   << "}}";
        } else {
            output << '{'
                   << "\"type\":\"OPEN_ORDERS\"," 
                   << "\"payload\":" << joinOrders(value.orders)
                   << '}';
        }

        return output.str();
    }, response);
}

std::string serializeDbMessage(const DbMessage& message) {
    return std::visit([](const auto& value) -> std::string {
        using T = std::decay_t<decltype(value)>;
        std::ostringstream output;
        if constexpr (std::is_same_v<T, DbTradeAddedMessage>) {
            output << '{'
                   << "\"type\":\"TRADE_ADDED\"," 
                   << "\"data\":{"
                   << "\"market\":\"" << escapeJson(value.market) << "\"," 
                   << "\"id\":\"" << escapeJson(value.id) << "\"," 
                   << "\"isBuyerMaker\":" << (value.isBuyerMaker ? "true" : "false") << ','
                   << "\"price\":\"" << escapeJson(value.price) << "\"," 
                   << "\"quantity\":\"" << escapeJson(value.quantity) << "\"," 
                   << "\"quoteQuantity\":\"" << escapeJson(value.quoteQuantity) << "\"," 
                   << "\"timestamp\":" << value.timestamp
                   << "}}";
        } else {
            output << '{'
                   << "\"type\":\"ORDER_UPDATE\"," 
                   << "\"data\":{"
                   << "\"orderId\":\"" << escapeJson(value.orderId) << "\"," 
                   << "\"executedQty\":" << value.executedQty;
            if (value.market) {
                output << ",\"market\":\"" << escapeJson(*value.market) << "\"";
            }
            if (value.price) {
                output << ",\"price\":\"" << escapeJson(*value.price) << "\"";
            }
            if (value.quantity) {
                output << ",\"quantity\":\"" << escapeJson(*value.quantity) << "\"";
            }
            if (value.side) {
                output << ",\"side\":\"" << toString(*value.side) << "\"";
            }
            output << "}}";
        }
        return output.str();
    }, message);
}

std::string serializeWsMessage(const WsMessage& message) {
    return std::visit([](const auto& value) -> std::string {
        using T = std::decay_t<decltype(value)>;
        std::ostringstream output;
        if constexpr (std::is_same_v<T, WsDepthUpdateMessage>) {
            output << '{'
                   << "\"stream\":\"" << escapeJson(value.stream) << "\"," 
                   << "\"data\":{"
                   << "\"a\":" << joinDepthLevels(value.asks) << ','
                   << "\"b\":" << joinDepthLevels(value.bids) << ','
                   << "\"e\":\"depth\""
                   << "}}";
        } else {
            output << '{'
                   << "\"stream\":\"" << escapeJson(value.stream) << "\"," 
                   << "\"data\":{"
                   << "\"e\":\"trade\"," 
                   << "\"t\":" << value.tradeId << ','
                   << "\"m\":" << (value.marketSide ? "true" : "false") << ','
                   << "\"p\":\"" << escapeJson(value.price) << "\"," 
                   << "\"q\":\"" << escapeJson(value.quantity) << "\"," 
                   << "\"s\":\"" << escapeJson(value.symbol) << "\""
                   << "}}";
        }
        return output.str();
    }, message);
}

} // namespace whiskers
