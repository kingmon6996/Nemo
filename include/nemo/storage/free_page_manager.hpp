#ifndef NEMO_STORAGE_FREE_PAGE_MANAGER_HPP
#define NEMO_STORAGE_FREE_PAGE_MANAGER_HPP

#include "nemo/core/ids.hpp"
#include "nemo/core/result.hpp"
#include <mutex>
#include <unordered_set>
#include <vector>

namespace nemo::storage {

// A skeleton FreePageManager for Module 2.
// Initially, this just acts as an in-memory tracker for freed pages to be reused.
// Future modules will persist this state.
class FreePageManager {
public:
    FreePageManager() = default;
    
    // Disable copy for simplicity right now
    FreePageManager(const FreePageManager&) = delete;
    FreePageManager& operator=(const FreePageManager&) = delete;
    
    // Allow move
    FreePageManager(FreePageManager&& other) noexcept {
        std::lock_guard<std::mutex> lock(other.m_mutex);
        m_free_pages = std::move(other.m_free_pages);
        m_free_page_set = std::move(other.m_free_page_set);
    }
    
    FreePageManager& operator=(FreePageManager&& other) noexcept {
        if (this != &other) {
            std::scoped_lock lock(m_mutex, other.m_mutex);
            m_free_pages = std::move(other.m_free_pages);
            m_free_page_set = std::move(other.m_free_page_set);
        }
        return *this;
    }

    Result<PageID> allocate();
    Result<void> release(PageID page_id);
    bool is_free(PageID page_id) const;
    std::vector<PageID> snapshot() const;
    Result<void> restore(std::vector<PageID> free_pages);

private:
    mutable std::mutex m_mutex;
    std::vector<PageID> m_free_pages;
    std::unordered_set<PageID> m_free_page_set;
};

} // namespace nemo::storage

#endif // NEMO_STORAGE_FREE_PAGE_MANAGER_HPP
