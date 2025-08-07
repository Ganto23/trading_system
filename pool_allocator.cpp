#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <string>
#include <new>
#include <type_traits>

struct DummyMutex {
    void lock() {}
    void unlock() {}
};

template <typename T, size_t N, bool ThreadSafe = true>
class PoolAllocator {
public:
    PoolAllocator() {
        m_pool = new char[N * sizeof(T)];
        for (size_t i = 0; i < N - 1; ++i) {
            T* current = reinterpret_cast<T*>(&m_pool[i * sizeof(T)]);
            T* next = reinterpret_cast<T*>(&m_pool[(i + 1) * sizeof(T)]);
            *reinterpret_cast<T**>(current) = next;
        }
        T* last = reinterpret_cast<T*>(&m_pool[(N - 1) * sizeof(T)]);
        *reinterpret_cast<T**>(last) = nullptr;
        m_head_of_free_list = reinterpret_cast<T*>(m_pool);
    }

    ~PoolAllocator() {
        delete[] m_pool;
    }

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    T* allocate() {
        std::lock_guard<MutexType> lock(m_mutex);
        if (!m_head_of_free_list) {
            throw std::bad_alloc();
        }
        T* block = m_head_of_free_list;
        m_head_of_free_list = *reinterpret_cast<T**>(block);
        return block;
    }

    void deallocate(T* ptr) {
        std::lock_guard<MutexType> lock(m_mutex);
        *reinterpret_cast<T**>(ptr) = m_head_of_free_list;
        m_head_of_free_list = ptr;
    }

private:
    using MutexType = typename std::conditional<ThreadSafe, std::mutex, DummyMutex>::type;

    char* m_pool;
    T* m_head_of_free_list;
    MutexType m_mutex;
};

