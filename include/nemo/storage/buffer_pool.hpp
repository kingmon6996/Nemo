#ifndef NEMO_STORAGE_BUFFER_POOL_HPP
#define NEMO_STORAGE_BUFFER_POOL_HPP

#include "nemo/core/config.hpp"
#include "nemo/core/result.hpp"
#include "nemo/core/status.hpp"
#include "nemo/storage/disk_manager.hpp"
#include "nemo/storage/buffer_frame.hpp"
#include "nemo/storage/replacer.hpp"
#include "nemo/wal/wal_manager.hpp"
#include <unordered_map>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <list>

namespace nemo::storage {

class PageHandle;

struct BufferPoolStats {
    std::atomic<std::uint64_t> cache_hits{0};
    std::atomic<std::uint64_t> cache_misses{0};
    std::atomic<std::uint64_t> evictions{0};
    std::atomic<std::uint64_t> flushes{0};
    
    // Derived metric for debugging/monitoring
    double cache_hit_ratio() const {
        std::uint64_t hits = cache_hits.load(std::memory_order_relaxed);
        std::uint64_t misses = cache_misses.load(std::memory_order_relaxed);
        if (hits + misses == 0) return 0.0;
        return static_cast<double>(hits) / static_cast<double>(hits + misses);
    }
};

class BufferPool {
public:
    BufferPool(std::size_t pool_size, DiskManager* disk_manager, wal::WALManager* wal_manager = nullptr);
    ~BufferPool();

    // Disable copy/move
    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;
    BufferPool(BufferPool&&) = delete;
    BufferPool& operator=(BufferPool&&) = delete;

    Result<PageHandle> fetch_page(PageID page_id);
    Result<PageHandle> allocate_page();
    
    Result<void> flush_page(PageID page_id);
    Result<void> flush_all();

    const BufferPoolStats& stats() const noexcept { return m_stats; }
    
    // Re-expose DatabaseHeader from DiskManager
    const DatabaseHeader& get_database_header() const { return m_disk_manager->get_database_header(); }

    DiskManager* disk_manager() { return m_disk_manager; }

private:
    friend class PageHandle;

    void unpin_page(FrameID frame_id) noexcept;
    void mark_dirty(FrameID frame_id) noexcept;

    DiskManager* m_disk_manager;
    wal::WALManager* m_wal_manager{nullptr};
    std::size_t m_pool_size;
    std::size_t m_page_size;

    // Frame allocation and storage
    std::vector<std::unique_ptr<BufferFrame>> m_frames;
    std::unique_ptr<Replacer> m_replacer;

    // Synchronization and lookup
    std::mutex m_page_table_mutex;
    std::unordered_map<PageID, FrameID> m_page_table;
    std::list<FrameID> m_free_list; // Unused, completely empty frames

    BufferPoolStats m_stats;
};

} // namespace nemo::storage

#endif // NEMO_STORAGE_BUFFER_POOL_HPP
