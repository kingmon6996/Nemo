#include "nemo/storage/buffer_pool.hpp"
#include "nemo/storage/clock_replacer.hpp"
#include "nemo/storage/page_handle.hpp"
#include "nemo/logging/logger.hpp"
#include <algorithm>

namespace nemo::storage {

BufferPool::BufferPool(std::size_t pool_size, DiskManager* disk_manager, wal::WALManager* wal_manager)
    : m_disk_manager(disk_manager), m_wal_manager(wal_manager), m_pool_size(pool_size) {
    
    m_frames.resize(pool_size);
    m_replacer = std::make_unique<ClockReplacer>(m_pool_size);
    
    for (std::size_t i = 0; i < m_pool_size; ++i) {
        m_frames[i] = std::make_unique<BufferFrame>();
        m_frames[i]->set_id(FrameID(static_cast<std::uint32_t>(i)));
        m_free_list.push_back(FrameID(static_cast<std::uint32_t>(i)));
    }
}

BufferPool::~BufferPool() {
    auto res = flush_all();
    if (!res) {
        NEMO_LOG_ERROR("BufferPool", "Failed to flush all pages during destruction.");
    }
}

void BufferPool::unpin_page(FrameID frame_id) noexcept {
    if (frame_id.value() < m_pool_size) {
        if (m_frames[frame_id.value()]->pin_count() == 0) {
            m_replacer->unpin(frame_id);
        }
    }
}

Result<PageHandle> BufferPool::fetch_page(PageID page_id) {
    FrameID frame_id;
    {
        std::lock_guard<std::mutex> lock(m_page_table_mutex);
        auto it = m_page_table.find(page_id);
        
        if (it != m_page_table.end()) {
            // CACHE HIT
            frame_id = it->second;
            m_stats.cache_hits.fetch_add(1, std::memory_order_relaxed);
            
            // Pin it while under page table lock to prevent eviction racing
            m_frames[frame_id.value()]->pin();
            m_replacer->pin(frame_id);
            m_replacer->record_access(frame_id);
            
            return PageHandle(this, m_frames[frame_id.value()].get());
        }
        
        // CACHE MISS
        m_stats.cache_misses.fetch_add(1, std::memory_order_relaxed);
        
        // 1. Find a victim frame
        if (!m_free_list.empty()) {
            frame_id = m_free_list.front();
            m_free_list.pop_front();
        } else {
            auto victim_opt = m_replacer->victim();
            if (!victim_opt) {
                return Result<PageHandle>(Status::InternalError);
            }
            frame_id = victim_opt.value();
            m_stats.evictions.fetch_add(1, std::memory_order_relaxed);
        }
        
        // 2. Remove old entry from page table if valid
        auto& frame = m_frames[frame_id.value()];
        if (frame->is_valid()) {
            m_page_table.erase(frame->page_id());
        }
        
        // 3. Mark the new entry as "in transit" by adding it to the table but leaving 
        // it locked in its frame mutex. Pin it so it can't be evicted.
        frame->pin();
        m_replacer->pin(frame_id);
        m_page_table[page_id] = frame_id;
    } // End page table lock

    // 4. Safely perform disk I/O outside the global page table lock
    auto& frame = m_frames[frame_id.value()];
    std::lock_guard<std::mutex> io_lock(frame->io_mutex());

    // Flush dirty page if needed
    if (frame->is_valid() && frame->is_dirty()) {
        // WAL-before-data rule
        if (m_wal_manager) {
            wal::LSN page_lsn = wal::LSN(frame->page().read_header().lsn);
            if (page_lsn.is_valid()) {
                m_wal_manager->flush_up_to(page_lsn);
            }
        }
        
        auto write_res = m_disk_manager->write_page(frame->page());
        if (!write_res) {
            // Unpin, cleanup, and return error
            frame->unpin();
            unpin_page(frame_id);
            std::lock_guard<std::mutex> lock(m_page_table_mutex);
            m_page_table.erase(page_id);
            m_free_list.push_back(frame_id);
            return Result<PageHandle>(write_res.error());
        }
        frame->set_dirty(false);
        m_stats.flushes.fetch_add(1, std::memory_order_relaxed);
    }

    // Read new page
    auto read_res = m_disk_manager->read_page(page_id);
    if (!read_res) {
        frame->unpin();
        unpin_page(frame_id);
        std::lock_guard<std::mutex> lock(m_page_table_mutex);
        m_page_table.erase(page_id);
        m_free_list.push_back(frame_id);
        return Result<PageHandle>(read_res.error());
    }

    // Move data into frame
    frame->page() = std::move(read_res.value());
    frame->set_page_id(page_id);
    frame->set_valid(true);
    frame->set_dirty(false);
    
    m_replacer->record_access(frame_id);

    return PageHandle(this, frame.get());
}

Result<PageHandle> BufferPool::allocate_page() {
    auto id_res = m_disk_manager->allocate_page();
    if (!id_res) return Result<PageHandle>(id_res.error());
    
    PageID new_page_id = id_res.value();
    
    // Once allocated on disk (filled with zeroes and valid PageHeader),
    // we fetch it into the buffer pool.
    return fetch_page(new_page_id);
}

Result<void> BufferPool::flush_page(PageID page_id) {
    FrameID frame_id;
    
    {
        std::lock_guard<std::mutex> lock(m_page_table_mutex);
        auto it = m_page_table.find(page_id);
        if (it == m_page_table.end()) {
            return Result<void>::success(); // Not in memory, nothing to flush.
        }
        frame_id = it->second;
    }
    
    auto& frame = m_frames[frame_id.value()];
    std::lock_guard<std::mutex> io_lock(frame->io_mutex());
    
    if (frame->is_valid() && frame->page_id() == page_id && frame->is_dirty()) {
        // WAL-before-data rule
        if (m_wal_manager) {
            wal::LSN page_lsn = wal::LSN(frame->page().read_header().lsn);
            if (page_lsn.is_valid()) {
                m_wal_manager->flush_up_to(page_lsn);
            }
        }
        
        auto write_res = m_disk_manager->write_page(frame->page());
        if (!write_res) return write_res;
        
        frame->set_dirty(false);
        m_stats.flushes.fetch_add(1, std::memory_order_relaxed);
    }
    
    return Result<void>::success();
}

Result<void> BufferPool::flush_all() {
    std::lock_guard<std::mutex> page_lock(m_page_table_mutex);
    
    for (const auto& [page_id, frame_id] : m_page_table) {
        auto& frame = m_frames[frame_id.value()];
        std::lock_guard<std::mutex> io_lock(frame->io_mutex());
        
        if (frame->is_valid() && frame->is_dirty()) {
            // WAL-before-data rule
            if (m_wal_manager) {
                wal::LSN page_lsn = wal::LSN(frame->page().read_header().lsn);
                if (page_lsn.is_valid()) {
                    m_wal_manager->flush_up_to(page_lsn);
                }
            }
            
            auto write_res = m_disk_manager->write_page(frame->page());
            if (!write_res) {
                // Keep trying to flush others but remember we failed
                NEMO_LOG_ERROR("BufferPool", "Failed to flush page during flush_all");
                return write_res;
            }
            frame->set_dirty(false);
            m_stats.flushes.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    return Result<void>::success();
}

} // namespace nemo::storage
