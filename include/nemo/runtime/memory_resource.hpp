#ifndef NEMO_RUNTIME_MEMORY_RESOURCE_HPP
#define NEMO_RUNTIME_MEMORY_RESOURCE_HPP

#include <memory_resource>
#include <atomic>
#include <cstddef>
#include <stdexcept>

namespace nemo::runtime {

class MemoryLimitExceeded : public std::runtime_error {
public:
    MemoryLimitExceeded(const std::string& what_arg) : std::runtime_error(what_arg) {}
};

class QueryMemoryResource : public std::pmr::memory_resource {
public:
    explicit QueryMemoryResource(std::size_t limit_bytes, std::pmr::memory_resource* upstream = std::pmr::get_default_resource())
        : m_limit(limit_bytes), m_upstream(upstream) {}

    std::size_t usage() const noexcept { return m_usage.load(std::memory_order_relaxed); }
    std::size_t limit() const noexcept { return m_limit; }
    
    void reset_usage() noexcept { m_usage.store(0, std::memory_order_relaxed); }

protected:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        std::size_t current = m_usage.load(std::memory_order_relaxed);
        while (true) {
            if (current + bytes > m_limit) {
                throw MemoryLimitExceeded("Query memory limit exceeded");
            }
            if (m_usage.compare_exchange_weak(current, current + bytes, std::memory_order_relaxed)) {
                break;
            }
        }
        
        try {
            return m_upstream->allocate(bytes, alignment);
        } catch (...) {
            m_usage.fetch_sub(bytes, std::memory_order_relaxed);
            throw;
        }
    }

    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        m_upstream->deallocate(p, bytes, alignment);
        m_usage.fetch_sub(bytes, std::memory_order_relaxed);
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

private:
    std::size_t m_limit;
    std::pmr::memory_resource* m_upstream;
    std::atomic<std::size_t> m_usage{0};
};

} // namespace nemo::runtime

#endif // NEMO_RUNTIME_MEMORY_RESOURCE_HPP
