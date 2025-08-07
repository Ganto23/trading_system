#pragma once

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
        // Ensure proper alignment
        static_assert(sizeof(T) >= sizeof(void*), "Type T must be at least pointer-sized");
        static_assert(alignof(T) <= alignof(std::max_align_t), "Type T alignment too large");
        
        m_pool = new(std::align_val_t{alignof(T)}) char[N * sizeof(T)];
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
        operator delete[](m_pool, std::align_val_t{alignof(T)});
    }

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    T* allocate() {
        std::lock_guard<MutexType> lock(m_mutex);
        if (!m_head_of_free_list) {
            return nullptr;
        }
        T* block = m_head_of_free_list;
        m_head_of_free_list = *reinterpret_cast<T**>(block);
        return block;
    }

    void deallocate(T* ptr) {
        if (!ptr) return;

        if (!isValidPointer(ptr)) return;

        std::lock_guard<MutexType> lock(m_mutex);
        *reinterpret_cast<T**>(ptr) = m_head_of_free_list;
        m_head_of_free_list = ptr;
    }

    size_t getPoolSize() const { return N; }

    size_t getAvailableCount() const {
        std::lock_guard<MutexType> lock(m_mutex);
        size_t count = 0;
        T* current = m_head_of_free_list;
        while (current) {
            count++;
            current = *reinterpret_cast<T**>(current);
        }
        return count;
    }

    bool isFull() const {
        std::lock_guard<MutexType> lock(m_mutex);
        return m_head_of_free_list == nullptr;
    }

    // Construction/Destruction Support
    template<typename... Args>
    T* construct(Args&&... args) {
        T* ptr = allocate();
        if (ptr) {
            new(ptr) T(std::forward<Args>(args)...); // Placement new
            #ifdef DEBUG
            ++m_constructions;
            #endif
        }
        return ptr;
    }
    
    void destroy(T* ptr) {
        if (ptr && isValidPointer(ptr)) {
            ptr->~T(); // Call destructor
            #ifdef DEBUG
            ++m_destructions;
            #endif
            deallocate(ptr);
        }
    }

    #ifdef DEBUG
    void printStats() const {
        std::lock_guard<MutexType> lock(m_mutex);
        std::cout << "Pool Stats - Constructions: " << m_constructions 
                  << ", Destructions: " << m_destructions 
                  << ", Available: " << getAvailableCount() << std::endl;
    }
    #endif

private:
    using MutexType = typename std::conditional<ThreadSafe, std::mutex, DummyMutex>::type;

    bool isValidPointer(T* ptr) const {
        char* char_ptr = reinterpret_cast<char*>(ptr);
        return char_ptr >= m_pool && 
               char_ptr < m_pool + (N * sizeof(T)) &&
               (char_ptr - m_pool) % sizeof(T) == 0;
    }

    char* m_pool;
    T* m_head_of_free_list;
    MutexType m_mutex;
    
    #ifdef DEBUG
    mutable size_t m_constructions = 0;
    mutable size_t m_destructions = 0;
    #endif
};
