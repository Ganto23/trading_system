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

    uint64_t submitOrder(double price, uint32_t quantity, bool is_buy);
    bool cancelOrder(uint64_t id);
    bool modifyOrder(uint64_t id, double new_price, uint32_t new_quantity);
    OrderStatus getOrderStatus(uint64_t id);
    void getOrderBookSnapshot(std::vector<Order>& bid_snapshot, std::vector<Order>& ask_snapshot);
    const std::vector<Trade>& getTradeHistory() const;

private:
    std::map<double, PriceLevel, std::greater<double>> bids;
    std::map<double, PriceLevel, std::less<double>> asks;
    std::vector<PoolAllocator<Order, 1024>*> pools;
    size_t current_pool = 0;
    std::unordered_map<uint64_t, Order*> order_lookup;
    std::vector<Trade> trade_history;

    std::shared_mutex bids_mutex;
    std::shared_mutex asks_mutex;
    std::shared_mutex order_lookup_mutex;
    mutable std::shared_mutex trade_history_mutex;
    std::shared_mutex pools_mutex;

    std::atomic<uint64_t> next_order_id = 1;

    Order* createOrder(uint64_t id, double price, uint32_t quantity, bool is_buy);
    uint64_t generateOrderId();
    uint64_t getUnixTimestamp() const;
    void matchOrders(uint64_t timestamp = 0);
    Order* getOrderById(uint64_t id);
    void removeOrderFromBook(Order* order);
    void destroyOrder(Order* order);
};