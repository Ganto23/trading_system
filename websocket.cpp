// Simple WebSocket server scaffold using uWebSockets
// You need to install uWebSockets and link it to your project.
// This example assumes you have an OrderBook instance available.

#include <uWebSockets/App.h>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp> // Install with vcpkg or add to your project
#include "order-book.h"
#include <unordered_set>
#include <unordered_map>

// Forward declare ClientData so we can define globals after
struct ClientData;

// Track all connected clients and map orders to owners
static std::unordered_set<uWS::WebSocket<false, true, ClientData>*> connected_clients;
static std::unordered_map<uint64_t, ClientData*> order_to_client; // moved global

OrderBook orderBook; // Global instance

using json = nlohmann::json;

struct ClientData {
    bool authenticated = false;
    std::unordered_set<uint64_t> my_orders;
    double realized_pnl = 0.0;
    double unrealized_pnl = 0.0;
};

void broadcastOrderBookSnapshot() {
    std::vector<Order> bid_snapshot, ask_snapshot;
    orderBook.getOrderBookSnapshot(bid_snapshot, ask_snapshot);
    json ob_resp = {
        {"type", "order_book_snapshot_response"},
        {"bids", json::array()},
        {"asks", json::array()}
    };
    for (const auto& o : bid_snapshot) {
        ob_resp["bids"].push_back({{"id", o.id}, {"price", o.price}, {"quantity", o.quantity}, {"is_buy", o.is_buy}, {"status", static_cast<int>(o.status)}});
    }
    for (const auto& o : ask_snapshot) {
        ob_resp["asks"].push_back({{"id", o.id}, {"price", o.price}, {"quantity", o.quantity}, {"is_buy", o.is_buy}, {"status", static_cast<int>(o.status)}});
    }
    for (auto* client : connected_clients) {
        client->send(ob_resp.dump());
    }
}

// Helper function to count open orders for a user
size_t getOpenOrdersCount(const ClientData* client) {
    size_t count = 0;
    for (auto id : client->my_orders) {
        if (orderBook.getOrderStatus(id) == OrderStatus::Open) {
            ++count;
        }
    }
    return count;
}
// Helper function to get best bid (highest price)
double getBestBid() {
    std::vector<Order> bid_snapshot, ask_snapshot;
    orderBook.getOrderBookSnapshot(bid_snapshot, ask_snapshot);
    if (!bid_snapshot.empty()) {
        return bid_snapshot.front().price;
    }
    return 0.0;
}

// Helper function to get best ask (lowest price)
double getBestAsk() {
    std::vector<Order> bid_snapshot, ask_snapshot;
    orderBook.getOrderBookSnapshot(bid_snapshot, ask_snapshot);
    if (!ask_snapshot.empty()) {
        return ask_snapshot.front().price;
    }
    return 0.0;
}

// Helper function to calculate unrealized PnL for a user
double getUnrealizedPnL(const ClientData* client) {
    double pnl = 0.0;
    for (auto id : client->my_orders) {
        OrderStatus status = orderBook.getOrderStatus(id);
        if (status == OrderStatus::Open) {
            const Order* order = orderBook.getOrderById(id);
            if (order) {
                double market_price = order->is_buy ? getBestAsk() : getBestBid();
                if (market_price > 0) {
                    pnl += (market_price - order->price) * order->quantity * (order->is_buy ? 1 : -1);
                }
            }
        }
    }
    return pnl;
}

int main() {
    uWS::App app;

    // Set realized PnL update callback (broadcast after each trade)
    orderBook.onTradePnLUpdate = [](uint64_t order_id, bool is_buy, double price, uint32_t qty) {
        auto it = order_to_client.find(order_id);
        if (it != order_to_client.end() && it->second) {
            ClientData* client = it->second;
            // Realized PnL: (sell - buy) * qty for buy, (buy - sell) * qty for sell
            // For buy: profit = (trade_price - order_price) * qty
            // For sell: profit = (order_price - trade_price) * qty
            const Order* order = orderBook.getOrderById(order_id);
            if (order) {
                double pnl = (is_buy ? (price - order->price) : (order->price - price)) * qty;
                client->realized_pnl += pnl;
            }
        }
        broadcastOrderBookSnapshot();
    };
    app.ws<ClientData>("/*", {
        // Handle new client connection
        .open = [](auto* ws) {
            ws->getUserData()->authenticated = false;
            ws->send(R"({"type":"welcome","message":"Please authenticate"})");
            connected_clients.insert(ws);
        },
        // Handle incoming messages
        .message = [](auto* ws, std::string_view msg, uWS::OpCode opCode) {
            try {
                json j = json::parse(msg);
                std::string type = j.value("type", "");
                json response;

                // Authentication check
                if (!ws->getUserData()->authenticated && type != "auth") {
                    ws->send(R"({"type":"error","message":"Not authenticated"})");
                    return;
                }

                if (type == "auth") {
                    std::string token = j.value("token", "");
                    // Replace "your_secret_token" with your real token or validation logic
                    if (token == "your_secret_token") {
                        ws->getUserData()->authenticated = true;
                        response = {{"type", "auth_response"}, {"success", true}};
                    } else {
                        response = {{"type", "auth_response"}, {"success", false}, {"message", "Invalid token"}};
                    }
                } else if (type == "submit") {
                    // Check for required fields and types
                    if (!j.contains("price") || !j.contains("qty") || !j.contains("is_buy")) {
                        response = {{"type", "error"}, {"message", "Missing required fields for submit"}};
                    } else if (!j["price"].is_number() || !j["qty"].is_number_unsigned() || !j["is_buy"].is_boolean()) {
                        response = {{"type", "error"}, {"message", "Invalid field types for submit"}};
                    } else {
                        double price = j["price"];
                        uint32_t qty = j["qty"];
                        bool is_buy = j["is_buy"];
                        uint64_t id = orderBook.submitOrder(price, qty, is_buy); // OrderBook now generates the ID
                        bool ok = (id != 0);
                        if (ok) {
                            ws->getUserData()->my_orders.insert(id);
                            order_to_client[id] = ws->getUserData();
                            broadcastOrderBookSnapshot();
                        }
                        response = {
                            {"type", "submit_response"},
                            {"success", ok},
                            {"id", id}
                        };
                    }
                } else if (type == "cancel") {
                    // Check for required fields and types
                    if (!j.contains("id") || !j["id"].is_number_unsigned()) {
                        response = {{"type", "error"}, {"message", "Missing or invalid id for cancel"}};
                    } else {
                        uint64_t id = j["id"];
                        if (!ws->getUserData()->my_orders.count(id)) {
                            response = {{"type", "cancel_response"}, {"success", false}, {"message", "Order not owned by user"}};
                        } else {
                            OrderStatus status = orderBook.getOrderStatus(id);
                            if (status == OrderStatus::NotFound) {
                                response = {{"type", "cancel_response"}, {"success", false}, {"message", "Order not found"}};
                            } else {
                                bool ok = orderBook.cancelOrder(id);
                                response = {{"type", "cancel_response"}, {"success", ok}};
                                if (ok) {
                                    ws->getUserData()->my_orders.erase(id);
                                    order_to_client.erase(id);
                                    broadcastOrderBookSnapshot();
                                }
                            }
                        }
                    }
                } else if (type == "modify") {
                    // Check for required fields and types
                    if (!j.contains("id") || !j["id"].is_number_unsigned() ||
                        !j.contains("price") || !j["price"].is_number() ||
                        !j.contains("qty") || !j["qty"].is_number_unsigned()) {
                        response = {{"type", "error"}, {"message", "Missing or invalid fields for modify"}};
                    } else {
                        uint64_t id = j["id"];
                        double price = j["price"];
                        uint32_t qty = j["qty"];
                        if (!ws->getUserData()->my_orders.count(id)) {
                            response = {{"type", "modify_response"}, {"success", false}, {"message", "Order not owned by user"}};
                        } else {
                            OrderStatus status = orderBook.getOrderStatus(id);
                            if (status == OrderStatus::NotFound) {
                                response = {{"type", "modify_response"}, {"success", false}, {"message", "Order not found"}};
                            } else {
                                bool ok = orderBook.modifyOrder(id, price, qty);
                                response = {{"type", "modify_response"}, {"success", ok}};
                                if (ok) {
                                    order_to_client[id] = ws->getUserData();
                                    broadcastOrderBookSnapshot();
                                } 
                            }
                        }
                    }
                } else if (type == "getOrderStatus") {
                    // Check for required fields and types
                    if (!j.contains("id") || !j["id"].is_number_unsigned()) {
                        response = {{"type", "error"}, {"message", "Missing or invalid id for getOrderStatus"}};
                    } else {
                        uint64_t id = j["id"];
                        if (!ws->getUserData()->my_orders.count(id)) {
                            response = {{"type", "order_status_response"}, {"success", false}, {"message", "Order not owned by user"}};
                        } else {
                            OrderStatus status = orderBook.getOrderStatus(id);
                            response = {
                                {"type", "order_status_response"},
                                {"id", id},
                                {"status", static_cast<int>(status)}
                            };
                        }
                    }
                } else if (type == "getOrderBookSnapshot") {
                    std::vector<Order> bid_snapshot, ask_snapshot;
                    orderBook.getOrderBookSnapshot(bid_snapshot, ask_snapshot);
                    response["type"] = "order_book_snapshot_response";
                    response["bids"] = json::array();
                    response["asks"] = json::array();
                    for (const auto& o : bid_snapshot) {
                        response["bids"].push_back({{"id", o.id}, {"price", o.price}, {"quantity", o.quantity}, {"is_buy", o.is_buy}, {"status", static_cast<int>(o.status)}});
                    }
                    for (const auto& o : ask_snapshot) {
                        response["asks"].push_back({{"id", o.id}, {"price", o.price}, {"quantity", o.quantity}, {"is_buy", o.is_buy}, {"status", static_cast<int>(o.status)}});
                    }
                } else if (type == "getTradeHistory") {
                    const auto& trades = orderBook.getTradeHistory();
                    response["type"] = "trade_history_response";
                    response["trades"] = json::array();
                    for (const auto& t : trades) {
                        response["trades"].push_back({{"buy_order_id", t.buy_order_id}, {"sell_order_id", t.sell_order_id}, {"price", t.price}, {"quantity", t.quantity}, {"timestamp", t.timestamp}});
                    }
                } else if (type == "getRealizedPnL") {
                    response = {
                        {"type", "realized_pnl_response"},
                        {"pnl", ws->getUserData()->realized_pnl}
                    };
                } else if (type == "getUnrealizedPnL") {
                    double pnl = getUnrealizedPnL(ws->getUserData());
                    response = {
                        {"type", "unrealized_pnl_response"},
                        {"pnl", pnl}
                    };
                } else if (type == "getOpenOrdersCount") {
                    size_t count = getOpenOrdersCount(ws->getUserData());
                    response = {
                        {"type", "open_orders_count_response"},
                        {"count", count}
                    };
                } else {
                    response = {
                        {"type", "error"},
                        {"message", "Unknown request type"}
                    };
                }
                ws->send(response.dump());
            } catch (const std::exception& e) {
                ws->send(R"({"type":"error","message":"Invalid JSON or missing fields"})");
            }
        },
        // Handle client disconnect
        .close = [](auto* ws, int code, std::string_view reason) {
            // Optional: log disconnects
            // Clean up order_to_client for this user
            for (auto id : ws->getUserData()->my_orders) {
                order_to_client.erase(id);
            }
            connected_clients.erase(ws);
        }
    }).listen("0.0.0.0", 9001, [](auto* listen_socket) {
        if (listen_socket) {
            std::cout << "Listening on port 9001" << std::endl;
        } else {
            std::cout << "Failed to listen on port 9001" << std::endl;
        }
    }).run();
    return 0;
}
