
#include "order-book.h"

OrderBook::OrderBook() {
    pools.push_back(new PoolAllocator<Order, 1024>());
}

OrderBook::~OrderBook() {
    for (auto pool : pools) delete pool;
}

Order* OrderBook::getOrderById(uint64_t id) {
    std::shared_lock lookup_lock(order_lookup_mutex);
    auto it = order_lookup.find(id);
    return (it != order_lookup.end()) ? it->second : nullptr;
}

Order* OrderBook::createOrder(uint64_t id, double price, uint32_t quantity, bool is_buy) {
    PoolAllocator<Order, 1024>* allocator = pools[current_pool];
    Order* order = allocator->allocate();
    if (!order) {
        pools.push_back(new PoolAllocator<Order, 1024>());
        current_pool++;
        allocator = pools[current_pool];
        order = allocator->allocate();
        if (!order) return nullptr;
    }
    order->id = id;
    order->price = price;
    order->quantity = quantity;
    order->is_buy = is_buy;
    order->pool_index = current_pool;
    order->status = OrderStatus::Open;
    order_lookup[id] = order;
    return order;
}

void OrderBook::destroyOrder(Order* order) {
    {
        std::shared_lock pools_lock(pools_mutex);
        pools[order->pool_index]->deallocate(order);
    }
    {
        std::unique_lock lookup_lock(order_lookup_mutex);
        order_lookup.erase(order->id);
    }
}

void OrderBook::removeOrderFromBook(Order* order) {
    if (order->is_buy) {
        std::unique_lock<std::shared_mutex> bids_lock(bids_mutex);
        auto it = bids.find(order->price);
        if (it != bids.end()) {
            auto& level = it->second;
            auto id_it = level.id_map.find(order->id);
            if (id_it != level.id_map.end()) {
                level.orders.erase(id_it->second);
                level.id_map.erase(id_it);
            }
            if (level.orders.empty()) bids.erase(it);
        }
    } else {
        std::unique_lock<std::shared_mutex> asks_lock(asks_mutex);
        auto it = asks.find(order->price);
        if (it != asks.end()) {
            auto& level = it->second;
            auto id_it = level.id_map.find(order->id);
            if (id_it != level.id_map.end()) {
                level.orders.erase(id_it->second);
                level.id_map.erase(id_it);
            }
            if (level.orders.empty()) asks.erase(it);
        }
    }
}

uint64_t OrderBook::getUnixTimestamp() const {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

uint64_t OrderBook::generateOrderId() {
    return next_order_id++;
}

uint64_t OrderBook::submitOrder(double price, uint32_t quantity, bool is_buy) {
    if (price <= 0 || quantity == 0) return 0;
    uint64_t id = generateOrderId();
    {
        std::unique_lock lock(order_lookup_mutex);
        if (order_lookup.count(id)) return 0;
    }
    Order* order = createOrder(id, price, quantity, is_buy);
    if (!order) return 0;
    if (is_buy) {
        std::unique_lock<std::shared_mutex> bids_lock(bids_mutex);
        auto& level = bids[price];
        level.orders.push_back(order);
        level.id_map[order->id] = std::prev(level.orders.end());
    } else {
        std::unique_lock<std::shared_mutex> asks_lock(asks_mutex);
        auto& level = asks[price];
        level.orders.push_back(order);
        level.id_map[order->id] = std::prev(level.orders.end());
    }
    matchOrders(getUnixTimestamp());
    return id;
}

bool OrderBook::cancelOrder(uint64_t id) {
    Order* order = nullptr;
    {
        std::unique_lock lookup_lock(order_lookup_mutex);
        order = getOrderById(id);
        if (!order || order->status != OrderStatus::Open) return false;
        order->status = OrderStatus::Canceled;
    }
    removeOrderFromBook(order);
    destroyOrder(order);
    return true;
}

bool OrderBook::modifyOrder(uint64_t id, double new_price, uint32_t new_quantity) {
    if (new_price <= 0 || new_quantity == 0) return false;
    Order* order = nullptr;
    {
        std::unique_lock<std::shared_mutex> lookup_lock(order_lookup_mutex);
        order = getOrderById(id);
        if (!order || order->status != OrderStatus::Open) return false;
    }
    removeOrderFromBook(order);
    order->price = new_price;
    order->quantity = new_quantity;
    if (order->is_buy) {
        std::unique_lock<std::shared_mutex> bids_lock(bids_mutex);
        auto& level = bids[new_price];
        level.orders.push_back(order);
        level.id_map[order->id] = std::prev(level.orders.end());
    } else {
        std::unique_lock<std::shared_mutex> asks_lock(asks_mutex);
        auto& level = asks[new_price];
        level.orders.push_back(order);
        level.id_map[order->id] = std::prev(level.orders.end());
    }
    matchOrders(getUnixTimestamp());
    return true;
}

OrderStatus OrderBook::getOrderStatus(uint64_t id) {
    std::shared_lock lock(order_lookup_mutex);
    Order* order = getOrderById(id);
    return order ? order->status : OrderStatus::NotFound;
}

void OrderBook::getOrderBookSnapshot(std::vector<Order>& bid_snapshot, std::vector<Order>& ask_snapshot) {
    {
        std::shared_lock bids_lock(bids_mutex);
        for (const auto& [price, level] : bids) {
            for (const auto& order : level.orders) {
                if (order->status == OrderStatus::Open)
                    bid_snapshot.push_back(*order);
            }
        }
    }
    {
        std::shared_lock asks_lock(asks_mutex);
        for (const auto& [price, level] : asks) {
            for (const auto& order : level.orders) {
                if (order->status == OrderStatus::Open)
                    ask_snapshot.push_back(*order);
            }
        }
    }
}

const std::vector<Trade>& OrderBook::getTradeHistory() const {
    std::shared_lock trade_lock(trade_history_mutex);
    return trade_history;
}

void OrderBook::matchOrders(uint64_t timestamp) {
    std::unique_lock bids_lock(bids_mutex);
    std::unique_lock asks_lock(asks_mutex);
    if (bids.empty() || asks.empty()) return;
    auto bid_it = bids.begin();
    auto ask_it = asks.begin();
    if (bid_it->first < ask_it->first) return;
    while (!bids.empty() && !asks.empty()) {
        bid_it = bids.begin();
        ask_it = asks.begin();
        double bid_price = bid_it->first;
        double ask_price = ask_it->first;
        if (bid_price < ask_price) break;
        auto& bid_level = bid_it->second;
        auto& ask_level = ask_it->second;
        if (bid_level.orders.empty()) {
            bids.erase(bid_it);
            continue;
        }
        if (ask_level.orders.empty()) {
            asks.erase(ask_it);
            continue;
        }
        Order* buy_order = bid_level.orders.front();
        Order* sell_order = ask_level.orders.front();
        uint32_t trade_qty = std::min(buy_order->quantity, sell_order->quantity);
        double trade_price = sell_order->price;
        {
            std::unique_lock trade_lock(trade_history_mutex);
            trade_history.push_back({buy_order->id, sell_order->id, trade_price, trade_qty, timestamp});
        }
        if (onTradePnLUpdate) {
            onTradePnLUpdate(buy_order->id, true, trade_price, trade_qty);
            onTradePnLUpdate(sell_order->id, false, trade_price, trade_qty);
        }
        buy_order->quantity -= trade_qty;
        sell_order->quantity -= trade_qty;
        if (buy_order->quantity == 0) {
            buy_order->status = OrderStatus::Filled;
            bid_level.id_map.erase(buy_order->id);
            bid_level.orders.pop_front();
            destroyOrder(buy_order);
        }
        if (sell_order->quantity == 0) {
            sell_order->status = OrderStatus::Filled;
            ask_level.id_map.erase(sell_order->id);
            ask_level.orders.pop_front();
            destroyOrder(sell_order);
        }
        if (bid_level.orders.empty()) bids.erase(bid_it);
        if (ask_level.orders.empty()) asks.erase(ask_it);
    }
}
