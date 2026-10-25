#include "nemo/storage/page_handle.hpp"
#include "nemo/storage/buffer_pool.hpp"
#include <cassert>

namespace nemo::storage {

PageHandle::~PageHandle() {
    unpin();
}

PageHandle::PageHandle(PageHandle&& other) noexcept 
    : m_pool(other.m_pool), m_frame(other.m_frame) 
{
    other.m_pool = nullptr;
    other.m_frame = nullptr;
}

PageHandle& PageHandle::operator=(PageHandle&& other) noexcept {
    if (this != &other) {
        unpin();
        m_pool = other.m_pool;
        m_frame = other.m_frame;
        other.m_pool = nullptr;
        other.m_frame = nullptr;
    }
    return *this;
}

const Page& PageHandle::get() const {
    assert(is_valid());
    return m_frame->page();
}

Page& PageHandle::get_mut() {
    assert(is_valid());
    mark_dirty();
    return m_frame->page();
}

void PageHandle::mark_dirty() {
    if (m_frame && m_pool) {
        m_frame->set_dirty(true);
    }
}

void PageHandle::unpin() {
    if (m_frame && m_pool) {
        // Decrement frame's atomic pin count
        m_frame->unpin();
        
        // Notify buffer pool's replacer if the pin count reached zero
        // This keeps the PageHandle decoupled from Replacer implementation details
        m_pool->unpin_page(m_frame->id());
        
        m_frame = nullptr;
        m_pool = nullptr;
    }
}

} // namespace nemo::storage
