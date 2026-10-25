#ifndef NEMO_CONCURRENCY_PAGE_LATCH_HPP
#define NEMO_CONCURRENCY_PAGE_LATCH_HPP

#include <shared_mutex>

namespace nemo::concurrency {

// A lightweight wrapper around std::shared_mutex for protecting physical page memory.
// Should ONLY be held for microseconds/milliseconds during physical byte manipulation.
class PageLatch {
public:
    PageLatch() = default;
    
    // Non-copyable, non-movable due to mutex
    PageLatch(const PageLatch&) = delete;
    PageLatch& operator=(const PageLatch&) = delete;
    PageLatch(PageLatch&&) = delete;
    PageLatch& operator=(PageLatch&&) = delete;

    void lock_shared() { m_mutex.lock_shared(); }
    void unlock_shared() { m_mutex.unlock_shared(); }
    
    void lock_exclusive() { m_mutex.lock(); }
    void unlock_exclusive() { m_mutex.unlock(); }

private:
    std::shared_mutex m_mutex;
};

// RAII helper for read latch
class ReadLatchGuard {
public:
    explicit ReadLatchGuard(PageLatch& latch) : m_latch(latch) { m_latch.lock_shared(); }
    ~ReadLatchGuard() { m_latch.unlock_shared(); }
private:
    PageLatch& m_latch;
};

// RAII helper for write latch
class WriteLatchGuard {
public:
    explicit WriteLatchGuard(PageLatch& latch) : m_latch(latch) { m_latch.lock_exclusive(); }
    ~WriteLatchGuard() { m_latch.unlock_exclusive(); }
private:
    PageLatch& m_latch;
};

} // namespace nemo::concurrency

#endif // NEMO_CONCURRENCY_PAGE_LATCH_HPP
