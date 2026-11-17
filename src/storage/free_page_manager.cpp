#include "nemo/storage/free_page_manager.hpp"
namespace nemo::storage {

Result<PageID> FreePageManager::allocate() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_free_pages.empty()) {
        return Result<PageID>(Status::NotFound);
    }
    
    PageID id = m_free_pages.back();
    m_free_pages.pop_back();
    m_free_page_set.erase(id);
    return id;
}

Result<void> FreePageManager::release(PageID page_id) {
    if (!page_id.is_valid() || page_id.value() == 0) {
        return Result<void>(Status::InvalidArgument);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_free_page_set.insert(page_id).second) {
        return Result<void>(Status::InvalidState); // Already free
    }
    m_free_pages.push_back(page_id);
    return Result<void>::success();
}

bool FreePageManager::is_free(PageID page_id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_free_page_set.contains(page_id);
}

std::vector<PageID> FreePageManager::snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_free_pages;
}

Result<void> FreePageManager::restore(std::vector<PageID> free_pages) {
    std::unordered_set<PageID> unique;
    unique.reserve(free_pages.size());
    for (const auto page_id : free_pages) {
        if (!page_id.is_valid() || page_id.value() == 0 ||
            !unique.insert(page_id).second) {
            return Status::Corruption;
        }
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_free_pages = std::move(free_pages);
    m_free_page_set = std::move(unique);
    return {};
}

} // namespace nemo::storage
