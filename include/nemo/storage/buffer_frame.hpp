#ifndef NEMO_STORAGE_BUFFER_FRAME_HPP
#define NEMO_STORAGE_BUFFER_FRAME_HPP

#include "nemo/core/ids.hpp"
#include "nemo/storage/page.hpp"
#include "nemo/concurrency/page_latch.hpp"
#include <atomic>
#include <mutex>

namespace nemo::storage {

// Represents a physical memory slot in the Buffer Pool.
// Holds the page data and metadata for synchronization and cache eviction.
class BufferFrame {
public:
    BufferFrame() = default;

    // A frame shouldn't be copied or moved while in use.
    BufferFrame(const BufferFrame&) = delete;
    BufferFrame& operator=(const BufferFrame&) = delete;

    FrameID id() const noexcept { return m_frame_id; }
    void set_id(FrameID id) noexcept { m_frame_id = id; }

    PageID page_id() const noexcept { return m_page_id; }
    void set_page_id(PageID id) noexcept { m_page_id = id; }

    bool is_valid() const noexcept { return m_is_valid; }
    void set_valid(bool valid) noexcept { m_is_valid = valid; }

    bool is_dirty() const noexcept { return m_is_dirty.load(std::memory_order_acquire); }
    void set_dirty(bool dirty) noexcept { m_is_dirty.store(dirty, std::memory_order_release); }

    std::uint32_t pin_count() const noexcept { return m_pin_count.load(std::memory_order_acquire); }
    
    void pin() noexcept { 
        m_pin_count.fetch_add(1, std::memory_order_acq_rel); 
    }
    
    void unpin() noexcept { 
        m_pin_count.fetch_sub(1, std::memory_order_acq_rel); 
    }

    Page& page() noexcept { return m_page; }
    const Page& page() const noexcept { return m_page; }

    // Physical Page Latch for concurrent access
    concurrency::PageLatch& latch() { return m_latch; }

    // Mutex to protect I/O operations and major state changes on this specific frame.
    // This allows concurrent reads/writes to different frames without a global lock.
    std::mutex& io_mutex() { return m_io_mutex; }

private:
    FrameID m_frame_id;
    PageID m_page_id;
    bool m_is_valid{false};
    
    std::atomic<bool> m_is_dirty{false};
    std::atomic<std::uint32_t> m_pin_count{0};
    
    Page m_page;
    std::mutex m_io_mutex;
    concurrency::PageLatch m_latch;
};

} // namespace nemo::storage

#endif // NEMO_STORAGE_BUFFER_FRAME_HPP
