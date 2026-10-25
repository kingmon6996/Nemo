#ifndef NEMO_STORAGE_PAGE_HANDLE_HPP
#define NEMO_STORAGE_PAGE_HANDLE_HPP

#include "nemo/storage/buffer_frame.hpp"
#include "nemo/concurrency/page_latch.hpp"

namespace nemo::storage {

class BufferPool;

class PageHandle {
public:
    PageHandle() noexcept = default;
    
    PageHandle(BufferPool* pool, BufferFrame* frame) noexcept 
        : m_pool(pool), m_frame(frame) {}

    ~PageHandle();

    // Non-copyable
    PageHandle(const PageHandle&) = delete;
    PageHandle& operator=(const PageHandle&) = delete;

    // Move semantics
    PageHandle(PageHandle&& other) noexcept;
    PageHandle& operator=(PageHandle&& other) noexcept;

    bool is_valid() const noexcept { return m_frame != nullptr; }
    explicit operator bool() const noexcept { return is_valid(); }

    // Read access
    const Page& get() const;

    // Write access - automatically marks the frame as dirty
    Page& get_mut();

    // Can manually mark dirty if not using get_mut
    void mark_dirty();

    // Access to the physical page latch
    concurrency::PageLatch& latch() { return m_frame->latch(); }

private:
    void unpin();

    BufferPool* m_pool{nullptr};
    BufferFrame* m_frame{nullptr};
};

} // namespace nemo::storage

#endif // NEMO_STORAGE_PAGE_HANDLE_HPP
