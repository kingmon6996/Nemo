#ifndef NEMO_RUNTIME_MEMORY_RESOURCE_HPP
#define NEMO_RUNTIME_MEMORY_RESOURCE_HPP

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <new>

namespace nemo::runtime {

class MemoryLimitExceeded : public std::runtime_error {
public:
    explicit MemoryLimitExceeded(const std::string& what_arg) : std::runtime_error(what_arg) {}
};

class QueryMemoryResource {
public:
    explicit QueryMemoryResource(std::size_t limit_bytes)
        : m_limit(limit_bytes) {}

    std::size_t usage() const noexcept { return m_usage.load(std::memory_order_relaxed); }
    std::size_t limit() const noexcept { return m_limit; }
    
    void reset_usage() noexcept { m_usage.store(0, std::memory_order_relaxed); }

    void* allocate(std::size_t bytes, std::size_t /*alignment*/ = alignof(std::max_align_t)) {
        std::size_t current = m_usage.load(std::memory_order_relaxed);
        while (true) {
            if (current + bytes > m_limit) {
                throw MemoryLimitExceeded("Query memory limit exceeded");
            }
            if (m_usage.compare_exchange_weak(current, current + bytes, std::memory_order_relaxed)) {
                break;
            }
        }
        
        void* ptr = std::malloc(bytes);
        if (!ptr) {
            m_usage.fetch_sub(bytes, std::memory_order_relaxed);
            throw std::bad_alloc();
        }
        return ptr;
    }

    void deallocate(void* p, std::size_t bytes, std::size_t /*alignment*/ = alignof(std::max_align_t)) noexcept {
        if (p) {
            std::free(p);
            m_usage.fetch_sub(bytes, std::memory_order_relaxed);
        }
    }

private:
    std::size_t m_limit;
    std::atomic<std::size_t> m_usage{0};
};

} // namespace nemo::runtime

#endif // NEMO_RUNTIME_MEMORY_RESOURCE_HPP
