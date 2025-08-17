
#pragma once

#include <map>
#include <list>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <algorithm>
#include <shared_mutex>
#include <mutex>
#include <chrono>
#include <atomic>
#include <functional>
#include "pool_allocator.h"

enum class OrderStatus { Open, Filled, Canceled, NotFound };

struct Order {
    uint64_t id;
    double price;
    uint32_t quantity;
    bool is_buy;
    size_t pool_index;
    OrderStatus status = OrderStatus::Open;
};

struct Trade {
    uint64_t buy_order_id;
    uint64_t sell_order_id;
    double price;
    uint32_t quantity;
    uint64_t timestamp;
};

struct PriceLevel {
    std::list<Order*> orders;
    std::unordered_map<uint64_t, std::list<Order*>::iterator> id_map;
};

class OrderBook {
public:
    OrderBook();
    ~OrderBook();

    // Expose getOrderById for external access
    Order* getOrderById(uint64_t id);

    // Price -> PriceLevel
    std::map<double, PriceLevel, std::greater<double>> bids;
    std::map<double, PriceLevel, std::less<double>> asks;

    // Multiple pools for dynamic expansion
    std::vector<PoolAllocator<Order, 1024>*> pools;
    size_t current_pool = 0;

    // Lookup for all orders by ID
    std::unordered_map<uint64_t, Order*> order_lookup;
    // Final status archive for orders that have been removed from memory (filled/canceled)
    std::unordered_map<uint64_t, OrderStatus> final_status_archive;

    // Trade history log
    std::vector<Trade> trade_history;

    std::shared_mutex bids_mutex;
    std::shared_mutex asks_mutex;
    std::shared_mutex order_lookup_mutex;
    mutable std::shared_mutex trade_history_mutex;
    std::shared_mutex pools_mutex;

    std::atomic<uint64_t> next_order_id = 1;

    uint64_t submitOrder(double price, uint32_t quantity, bool is_buy);
    bool cancelOrder(uint64_t id);
    bool modifyOrder(uint64_t id, double new_price, uint32_t new_quantity);
    OrderStatus getOrderStatus(uint64_t id);
    void getOrderBookSnapshot(std::vector<Order>& bid_snapshot, std::vector<Order>& ask_snapshot);
    // Return a copy to avoid exposing internal storage after releasing the lock
    std::vector<Trade> getTradeHistory() const;

    // Realized PnL update callback
    std::function<void(uint64_t, bool, double, uint32_t)> onTradePnLUpdate = nullptr;
    // Trade event callback (broadcast individual trade details externally)
    std::function<void(const Trade&)> onTradeEvent = nullptr;

    void removeOrderFromBook(Order* order);
    void destroyOrder(Order* order);

    // Helper to get current Unix timestamp
    uint64_t getUnixTimestamp() const;

    // Generate a unique order ID
    uint64_t generateOrderId();

    // Create a new order using the latest pool allocator
    Order* createOrder(uint64_t id, double price, uint32_t quantity, bool is_buy);

    // Match orders (simple matching engine)
    void matchOrders(uint64_t timestamp = 0);

private:
    
};

