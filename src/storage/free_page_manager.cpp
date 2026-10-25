#include "nemo/storage/free_page_manager.hpp"
#include <algorithm>

namespace nemo::storage {

Result<PageID> FreePageManager::allocate() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_free_pages.empty()) {
        return Result<PageID>(Status::NotFound);
    }
    
    PageID id = m_free_pages.back();
    m_free_pages.pop_back();
    return id;
}

Result<void> FreePageManager::release(PageID page_id) {
    if (!page_id.is_valid() || page_id.value() == 0) {
        return Result<void>(Status::InvalidArgument);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    // In a simple vector, releasing the same page twice should be prevented
    if (std::find(m_free_pages.begin(), m_free_pages.end(), page_id) != m_free_pages.end()) {
        return Result<void>(Status::InvalidState); // Already free
    }
    
    m_free_pages.push_back(page_id);
    return Result<void>::success();
}

bool FreePageManager::is_free(PageID page_id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::find(m_free_pages.begin(), m_free_pages.end(), page_id) != m_free_pages.end();
}

} // namespace nemo::storage
