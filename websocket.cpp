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
#include <vector>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <mutex>

#define LOG(msg) std::cerr << "[WS] " << msg << std::endl

// Forward declare ClientData so we can define globals after
struct ClientData;

// Track all connected clients and map orders to owners
static std::unordered_set<uWS::WebSocket<false, true, ClientData>*> connected_clients;
static std::unordered_map<uint64_t, ClientData*> order_to_client; // moved global
static std::vector<uint64_t> system_seed_orders; // track seeded liquidity order ids
static std::atomic<bool> snapshotDirty{false};
static std::atomic<bool> snapshotBroadcastScheduled{false};
static std::chrono::steady_clock::time_point lastSnapshotBroadcast = std::chrono::steady_clock::now();
static constexpr std::chrono::milliseconds SNAPSHOT_MIN_INTERVAL{100}; // throttle interval
// Cache entry prices to avoid orderBook lookups inside trade callback (prevent self-deadlock)
static std::unordered_map<uint64_t, double> order_entry_price;
static double last_trade_price = 0.0; // last executed trade price for marking
// Stats & shutdown tracking
static std::atomic<bool> shutdownRequested{false};
static std::atomic<bool> shutdownInProgress{false};
static uWS::Loop* g_loop = nullptr;
static std::atomic<uint64_t> stat_orders_submitted{0};
static std::atomic<uint64_t> stat_orders_canceled{0};
static std::atomic<uint64_t> stat_trade_events{0};
static std::atomic<uint64_t> stat_traded_quantity{0};
static std::mutex filled_set_mutex;
static std::unordered_set<uint64_t> filled_order_set;
static std::atomic<int> next_client_id{1};
// Simple per-client PnL query rate limiting
struct RateBucket { std::chrono::steady_clock::time_point windowStart; int count = 0; };
static std::unordered_map<ClientData*, RateBucket> pnlRate;

OrderBook orderBook; // Global instance

using json = nlohmann::json;

struct ClientData {
    bool authenticated = false;
    std::unordered_set<uint64_t> my_orders;
    double realized_pnl = 0.0;
    double unrealized_pnl = 0.0;
    int64_t position = 0;      // net position (>0 long, <0 short)
    double avg_cost = 0.0;     // average entry cost for current absolute position
    int client_id = 0;         // unique id for aggregation
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

// Broadcast a single trade event to all clients
void broadcastTradeEvent(const Trade& t) {
    json tr = {
        {"type", "trade"},
        {"buy_order_id", t.buy_order_id},
        {"sell_order_id", t.sell_order_id},
        {"price", t.price},
        {"quantity", t.quantity},
        {"timestamp", t.timestamp}
    };
    auto payload = tr.dump();
    for (auto* client : connected_clients) {
        client->send(payload);
    }
}

// Defer broadcasting to avoid holding any internal OrderBook locks while sending
void scheduleBroadcast() {
    snapshotDirty.store(true, std::memory_order_relaxed);
    bool expected = false;
    if (snapshotBroadcastScheduled.compare_exchange_strong(expected, true)) {
        // Defer to event loop; sends will happen outside of user handlers
        uWS::Loop::get()->defer([](){
            auto now = std::chrono::steady_clock::now();
            bool dirty = snapshotDirty.load(std::memory_order_relaxed);
            if (dirty && (now - lastSnapshotBroadcast) >= SNAPSHOT_MIN_INTERVAL) {
                snapshotDirty.store(false, std::memory_order_relaxed);
                lastSnapshotBroadcast = now;
                broadcastOrderBookSnapshot();
                snapshotBroadcastScheduled.store(false, std::memory_order_relaxed);
            } else {
                // Not enough time elapsed; allow future scheduling attempts
                snapshotBroadcastScheduled.store(false, std::memory_order_relaxed);
                // Keep dirty flag set so a later event schedules broadcast
            }
        });
    }
}

// Seed a symmetric price ladder if book is empty at startup.
// These orders are owned by the system (no client association) and provide initial liquidity.
void seedInitialBook(double mid_price = 100.0,
                     double tick = 0.5,
                     int levels_each_side = 5,
                     uint32_t base_qty = 10) {
    std::vector<Order> bids, asks;
    orderBook.getOrderBookSnapshot(bids, asks);
    if (!bids.empty() || !asks.empty()) {
        return; // already populated, skip
    }
    for (int i = 1; i <= levels_each_side; ++i) {
        double bid_price = mid_price - i * tick;
        double ask_price = mid_price + i * tick;
        uint64_t bid_id = orderBook.submitOrder(bid_price, base_qty, true);
        uint64_t ask_id = orderBook.submitOrder(ask_price, base_qty, false);
        if (bid_id) system_seed_orders.push_back(bid_id);
        if (ask_id) system_seed_orders.push_back(ask_id);
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

// Mark price fallback: prefer last trade, else mid, else best side
double markPriceFallback() {
    if (last_trade_price > 0) return last_trade_price;
    double bb = getBestBid();
    double ba = getBestAsk();
    if (bb > 0 && ba > 0) return (bb + ba) * 0.5;
    return (bb > 0 ? bb : ba);
}

// Helper function to calculate unrealized PnL (inventory + optional open order edge effect)
double getUnrealizedPnL(const ClientData* client) {
    double pnl = 0.0;
    // Inventory component
    if (client->position != 0 && client->avg_cost > 0) {
        double mark = markPriceFallback();
        if (mark > 0) {
            if (client->position > 0) {
                pnl += (mark - client->avg_cost) * client->position;
            } else { // short
                pnl += (client->avg_cost - mark) * (-client->position);
            }
        }
    }
    // Optional: open order component (can omit for pure inventory mark-to-market)
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

static json buildAllPnL() {
    json arr = json::array();
    for (auto* ws : connected_clients) {
        auto* cd = ws->getUserData();
        if (!cd || !cd->authenticated) continue;
        double unreal = getUnrealizedPnL(cd);
        arr.push_back({
            {"client_id", cd->client_id},
            {"position", cd->position},
            {"realized", cd->realized_pnl},
            {"unrealized", unreal},
            {"avg_cost", cd->avg_cost}
        });
    }
    return arr;
}

// Pretty separator helper
static void sep(const std::string& title) {
    std::cerr << "\n========== " << title << " ==========\n";
}

// Final stats printer (called on graceful shutdown)
void printFinalStats() {
    std::vector<Order> bid_snapshot, ask_snapshot;
    orderBook.getOrderBookSnapshot(bid_snapshot, ask_snapshot);
    size_t open_buy = 0, open_sell = 0;
    for (auto& o : bid_snapshot) if (o.status == OrderStatus::Open) ++open_buy;
    for (auto& o : ask_snapshot) if (o.status == OrderStatus::Open) ++open_sell;
    const auto& trades = orderBook.getTradeHistory();

    sep("SERVER SUMMARY");
    std::cerr << "Last trade price: " << last_trade_price << "\n";
    std::cerr << "Total trade prints: " << trades.size() << "\n";
    std::cerr << "Stat trade events (callback count): " << stat_trade_events.load() << "\n";
    std::cerr << "Total traded quantity: " << stat_traded_quantity.load() << "\n";
    std::cerr << "Orders submitted: " << stat_orders_submitted.load() << "\n";
    std::cerr << "Orders canceled: " << stat_orders_canceled.load() << "\n";
    {
        std::lock_guard<std::mutex> lk(filled_set_mutex);
        std::cerr << "Unique orders filled: " << filled_order_set.size() << "\n";
    }
    std::cerr << "Open buy orders: " << open_buy << " | Open sell orders: " << open_sell << "\n";

    sep("TOP OF BOOK");
    if (!bid_snapshot.empty())
        std::cerr << "Best Bid: " << bid_snapshot.front().price << " qty=" << bid_snapshot.front().quantity << "\n";
    else std::cerr << "Best Bid: (none)\n";
    if (!ask_snapshot.empty())
        std::cerr << "Best Ask: " << ask_snapshot.front().price << " qty=" << ask_snapshot.front().quantity << "\n";
    else std::cerr << "Best Ask: (none)\n";

    sep("FULL BIDS (price desc)");
    for (const auto& o : bid_snapshot) {
        std::cerr << "BID id=" << o.id << " px=" << o.price << " qty=" << o.quantity << " status=" << static_cast<int>(o.status) << "\n";
    }
    sep("FULL ASKS (price asc)");
    for (const auto& o : ask_snapshot) {
        std::cerr << "ASK id=" << o.id << " px=" << o.price << " qty=" << o.quantity << " status=" << static_cast<int>(o.status) << "\n";
    }

    sep("RECENT TRADES (last 20)");
    size_t start = trades.size() > 20 ? trades.size() - 20 : 0;
    for (size_t i = start; i < trades.size(); ++i) {
        const auto& t = trades[i];
        std::cerr << "Trade #" << i << " qty=" << t.quantity << " px=" << t.price
                  << " buyOrder=" << t.buy_order_id << " sellOrder=" << t.sell_order_id
                  << " ts=" << t.timestamp << "\n";
    }

    sep("CLIENT POSITIONS / PnL");
    for (auto* ws : connected_clients) {
        auto* cd = ws->getUserData();
        double unreal = getUnrealizedPnL(cd);
        std::cerr << "Client@" << cd << " pos=" << cd->position
                  << " avg_cost=" << cd->avg_cost
                  << " realized=" << cd->realized_pnl
                  << " unreal=" << unreal
                  << " open_orders=" << getOpenOrdersCount(cd)
                  << "\n";
    }
    sep("DONE");
}

// Signal handler (only sets flags or defers heavy work)
void handleSigInt(int) {
    if (!shutdownRequested.exchange(true)) {
        if (g_loop) {
            g_loop->defer([](){
                if (shutdownInProgress.exchange(true)) return;
                LOG("SIGINT received: generating final stats...");
                printFinalStats();
                LOG("Exiting after stats (first SIGINT).");
                std::exit(0);
            });
        } else {
            std::exit(0);
        }
    } else {
        std::_Exit(1); // force exit on second SIGINT
    }
}

int main() {
    // Register signal handler early
    std::signal(SIGINT, handleSigInt);
    // Seed initial book liquidity before accepting clients (configurable defaults)
    seedInitialBook(100.0, 0.5, 5, 10);
    LOG("Server starting; initial seed (if empty) applied");

    uWS::App app;
    g_loop = uWS::Loop::get();

    // Disable legacy per-order PnL callback (handled in onTradeEvent now)
    orderBook.onTradePnLUpdate = [](uint64_t, bool, double, uint32_t) {};

    // Comprehensive trade event callback: update positions, realized PnL, broadcast
    orderBook.onTradeEvent = [](const Trade& t){
        stat_trade_events.fetch_add(1, std::memory_order_relaxed);
        stat_traded_quantity.fetch_add(t.quantity, std::memory_order_relaxed);
        last_trade_price = t.price;
        auto handleSide = [&](uint64_t order_id, bool is_buy_side){
            auto itOwner = order_to_client.find(order_id);
            if (itOwner == order_to_client.end()) return;
            ClientData* cd = itOwner->second; if (!cd) return;
            int64_t pos = cd->position;
            double avg = cd->avg_cost;
            uint32_t qty = t.quantity;
            double px = t.price;
            if (pos == 0) avg = 0.0; // reset anchor
            if (is_buy_side) {
                if (pos < 0) { // covering short
                    uint32_t closing = std::min<uint32_t>(qty, static_cast<uint32_t>(-pos));
                    cd->realized_pnl += (avg - px) * closing; // short profit if avg>px
                    pos += closing; // less negative
                    uint32_t opening = qty - closing;
                    if (opening > 0) { pos += opening; avg = px; }
                    else if (pos == 0) avg = 0.0;
                } else { // adding / starting long
                    int64_t new_pos = pos + qty;
                    avg = (pos > 0) ? ((avg * pos) + (px * qty)) / new_pos : px;
                    pos = new_pos;
                }
            } else { // sell side
                if (pos > 0) { // reducing long
                    uint32_t closing = std::min<uint32_t>(qty, static_cast<uint32_t>(pos));
                    cd->realized_pnl += (px - avg) * closing; // long profit if px>avg
                    pos -= closing;
                    uint32_t opening = qty - closing;
                    if (opening > 0) { pos -= opening; avg = px; }
                    else if (pos == 0) avg = 0.0;
                } else { // adding / starting short
                    uint64_t absPos = static_cast<uint64_t>(-pos);
                    uint64_t new_abs = absPos + qty;
                    avg = (absPos > 0) ? ((avg * absPos) + (px * qty)) / new_abs : px;
                    pos -= qty;
                }
            }
            cd->position = pos;
            cd->avg_cost = avg;
        };

        // Update both sides (buy, sell)
        handleSide(t.buy_order_id, true);
        handleSide(t.sell_order_id, false);

        // Broadcast aggregate trade event
        try { broadcastTradeEvent(t); } catch (...) { LOG("Trade broadcast exception"); }

        // Send execution events to owners (optional for reducing status polling)
        auto sendExec = [&](uint64_t order_id, bool is_buy_side){
            auto itOwner = order_to_client.find(order_id);
            if (itOwner == order_to_client.end()) return;
            ClientData* cd = itOwner->second; if (!cd) return;
            for (auto* ws : connected_clients) {
                if (ws->getUserData() == cd) {
                    double unreal_exec = getUnrealizedPnL(cd); // compute fresh unrealized for push
                    json exec = {
                        {"type","execution"},
                        {"order_id", order_id},
                        {"side", is_buy_side ? "buy" : "sell"},
                        {"price", t.price},
                        {"quantity", t.quantity},
                        {"position", cd->position},
                        {"avg_cost", cd->avg_cost},
                        {"realized_pnl", cd->realized_pnl},
                        {"unrealized_pnl", unreal_exec}
                    };
                    ws->send(exec.dump());
                    break;
                }
            }
        };
        sendExec(t.buy_order_id, true);
        sendExec(t.sell_order_id, false);

        // Defer filled detection until after matching engine releases locks & statuses updated
        if (g_loop) {
            uint64_t bId = t.buy_order_id;
            uint64_t sId = t.sell_order_id;
            g_loop->defer([bId, sId](){
                auto check = [&](uint64_t id){
                    if (!id) return;
                    OrderStatus st = orderBook.getOrderStatus(id);
                    if (st == OrderStatus::Filled) {
                        std::lock_guard<std::mutex> lk(filled_set_mutex);
                        filled_order_set.insert(id);
                    }
                };
                check(bId);
                check(sId);
            });
        }

        scheduleBroadcast();
        // Broadcast multi-agent PnL snapshot
        try {
            json push = { {"type","all_pnl_push"}, {"clients", buildAllPnL()} };
            std::string s = push.dump();
            for (auto* ws : connected_clients) ws->send(s);
        } catch (...) { LOG("all_pnl_push broadcast error"); }
    };
    app.ws<ClientData>("/*", {
        // Handle new client connection
        .open = [](auto* ws) {
            ws->getUserData()->authenticated = false;
            ws->getUserData()->client_id = next_client_id.fetch_add(1);
            ws->send(R"({"type":"welcome","message":"Please authenticate"})");
            connected_clients.insert(ws);
            LOG("Client connected");
        },
        // Handle incoming messages
    .message = [](auto* ws, std::string_view msg, uWS::OpCode opCode) {
            LOG("Recv: " << msg);
            try {
                json j = json::parse(msg);
                std::string type = j.value("type", "");
                json response;
        bool triggerBroadcast = false;
        // Correlation id support: echo back any unsigned integer 'corr' provided in request
        uint64_t corr = 0; bool hasCorr = false;
        try {
            if (j.contains("corr") && j["corr"].is_number_unsigned()) { corr = j["corr"]; hasCorr = true; }
        } catch (...) { /* ignore corr extraction issues */ }

                // Authentication check
                if (!ws->getUserData()->authenticated && type != "auth") {
                    response = {{"type","error"},{"message","Not authenticated"}};
                    if (hasCorr) response["corr"] = corr;
                    ws->send(response.dump());
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
                    if (!j.contains("price") || !j.contains("qty") || !j.contains("is_buy")) {
                        response = {{"type", "error"}, {"message", "Missing required fields for submit"}};
                    } else if (!j["price"].is_number() || !j["qty"].is_number_unsigned() || !j["is_buy"].is_boolean()) {
                        response = {{"type", "error"}, {"message", "Invalid field types for submit"}};
                    } else {
                        double price = j["price"];
                        uint32_t qty = j["qty"];
                        bool is_buy = j["is_buy"];
                        LOG("Submit start side=" << (is_buy?"BUY":"SELL") << " px=" << price << " qty=" << qty);
                        uint64_t id = orderBook.submitOrder(price, qty, is_buy);
                        bool ok = (id != 0);
                        uint32_t filled_qty = 0;
                        OrderStatus final_status = OrderStatus::NotFound;
                        if (ok) {
                            stat_orders_submitted.fetch_add(1, std::memory_order_relaxed);
                            ws->getUserData()->my_orders.insert(id);
                            order_to_client[id] = ws->getUserData();
                            order_entry_price[id] = price; // cache entry price
                            final_status = orderBook.getOrderStatus(id);
                            const Order* ord_ptr = orderBook.getOrderById(id);
                            if (final_status == OrderStatus::Filled) {
                                filled_qty = qty;
                            } else if (ord_ptr) {
                                filled_qty = qty - ord_ptr->quantity;
                            }
                            triggerBroadcast = true;
                        }
                        response = {{"type", "submit_response"}, {"success", ok}, {"id", id}, {"filled_qty", filled_qty}, {"status", static_cast<int>(final_status)}};
                        LOG("Submit done id=" << id << " status=" << static_cast<int>(final_status) << " filled=" << filled_qty);
                    }
                } else if (type == "cancel") {
                    if (!j.contains("id") || !j["id"].is_number_unsigned()) {
                        response = {{"type","error"},{"message","Missing or invalid id for cancel"}};
                    } else {
                        uint64_t id = j["id"];
                        LOG("Cancel request id=" << id);
                        if (!ws->getUserData()->my_orders.count(id)) {
                            response = {{"type","cancel_response"},{"success",false},{"message","Order not owned by user"}};
                        } else {
                            auto start = std::chrono::steady_clock::now();
                            bool ok = orderBook.cancelOrder(id); // single attempt without pre-status query
                            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
                            OrderStatus after = ok ? OrderStatus::Canceled : orderBook.getOrderStatus(id);
                            if (ok) {
                                stat_orders_canceled.fetch_add(1, std::memory_order_relaxed);
                                ws->getUserData()->my_orders.erase(id);
                                order_to_client.erase(id);
                                order_entry_price.erase(id);
                                triggerBroadcast = true;
                            }
                            response = {{"type","cancel_response"},{"success",ok},{"status", static_cast<int>(after)},{"elapsed_ms", elapsed}};
                            LOG("Cancel done id=" << id << " ok=" << ok << " took=" << elapsed << "ms status=" << static_cast<int>(after));
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
                        LOG("Modify request id=" << id << " new_px=" << price << " new_qty=" << qty);
                        if (!ws->getUserData()->my_orders.count(id)) {
                            response = {{"type", "modify_response"}, {"success", false}, {"message", "Order not owned by user"}};
                        } else {
                            OrderStatus status = orderBook.getOrderStatus(id);
                            if (status == OrderStatus::NotFound) {
                                response = {{"type", "modify_response"}, {"success", false}, {"message", "Order not found"}};
                            } else if (status != OrderStatus::Open) {
                                response = {{"type", "modify_response"}, {"success", false}, {"message", "Order not open"}, {"status", static_cast<int>(status)}};
                            } else {
                                bool ok = orderBook.modifyOrder(id, price, qty);
                                OrderStatus newStatus = orderBook.getOrderStatus(id);
                                if (ok) {
                                    order_to_client[id] = ws->getUserData();
                                    order_entry_price[id] = price; // update cached price
                                    triggerBroadcast = true;
                                }
                                response = {{"type", "modify_response"}, {"success", ok}, {"status", static_cast<int>(newStatus)}};
                                LOG("Modify done id=" << id << " ok=" << ok << " newStatus=" << static_cast<int>(newStatus));
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
                            std::string status_text = (status == OrderStatus::Open ? "open" : status == OrderStatus::Filled ? "filled" : status == OrderStatus::Canceled ? "canceled" : "not_found");
                            response = {
                                {"type", "order_status_response"},
                                {"success", true},
                                {"id", id},
                                {"status", static_cast<int>(status)},
                                {"status_text", status_text}
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
                    auto* cd = ws->getUserData();
                    auto &bucket = pnlRate[cd];
                    auto now = std::chrono::steady_clock::now();
                    if (bucket.count == 0) bucket.windowStart = now;
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - bucket.windowStart).count();
                    if (elapsed > 1000) { bucket.windowStart = now; bucket.count = 0; }
                    if (++bucket.count > 5) {
                        response = {{"type","error"},{"message","PnL rate limit"}};
                    } else {
                        response = {
                            {"type", "realized_pnl_response"},
                            {"pnl", cd->realized_pnl}
                        };
                    }
                } else if (type == "getUnrealizedPnL") {
                    auto* cd = ws->getUserData();
                    auto &bucket = pnlRate[cd];
                    auto now = std::chrono::steady_clock::now();
                    if (bucket.count == 0) bucket.windowStart = now;
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - bucket.windowStart).count();
                    if (elapsed > 1000) { bucket.windowStart = now; bucket.count = 0; }
                    if (++bucket.count > 5) {
                        response = {{"type","error"},{"message","PnL rate limit"}};
                    } else {
                        double pnl = getUnrealizedPnL(cd);
                        response = {
                            {"type", "unrealized_pnl_response"},
                            {"pnl", pnl}
                        };
                    }
                } else if (type == "getAllPnL") {
                    response = {
                        {"type", "all_pnl_response"},
                        {"clients", buildAllPnL()}
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
                if (hasCorr) {
                    // Only tag responses that are direct replies (not broadcasts)
                    // All responses built above qualify here
                    response["corr"] = corr;
                }
                ws->send(response.dump());
                if (triggerBroadcast) {
                    scheduleBroadcast();
                }
            } catch (const std::exception& e) {
                LOG("Top-level message exception: " << e.what());
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
            pnlRate.erase(ws->getUserData());
            connected_clients.erase(ws);
            LOG("Client disconnected");
        }
    }).listen("0.0.0.0", 9001, [](auto* listen_socket) {
        if (listen_socket) {
            std::cout << "Listening on port 9001" << std::endl;
            LOG("Listening on 9001");
        } else {
            std::cout << "Failed to listen on port 9001" << std::endl;
            LOG("Failed to listen on 9001");
        }
    }).run();
    return 0;
}
