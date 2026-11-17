#include "nemo/storage/buffer_pool.hpp"
#include "nemo/storage/clock_replacer.hpp"
#include "nemo/storage/page_handle.hpp"
#include "nemo/storage/page_header.hpp"
#include "nemo/logging/logger.hpp"
#include <algorithm>
#include <exception>

namespace nemo::storage {
namespace {

Result<void> flush_page_wal(wal::WALManager* manager, const Page& page) {
    if (manager == nullptr) return {};
    const wal::LSN page_lsn(page.read_header().lsn);
    if (!page_lsn.is_valid()) return {};

    try {
        manager->flush_up_to(page_lsn);
        return {};
    } catch (const std::exception& error) {
        NEMO_LOG_ERROR("BufferPool", error.what());
        return Status::IOError;
    }
}

} // namespace

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
    PageID evicted_page_id{};
    bool evicted_page = false;
    {
        std::unique_lock<std::mutex> lock(m_page_table_mutex);
        for (;;) {
            const auto it = m_page_table.find(page_id);
            if (it != m_page_table.end()) {
                frame_id = it->second;
                m_stats.cache_hits.fetch_add(1, std::memory_order_relaxed);
                m_frames[frame_id.value()]->pin();
                m_replacer->pin(frame_id);
                m_replacer->record_access(frame_id);
                return PageHandle(this, m_frames[frame_id.value()].get());
            }

            if (m_loading_pages.find(page_id) == m_loading_pages.end()) break;
            m_page_table_cv.wait(lock, [&] {
                return m_loading_pages.find(page_id) == m_loading_pages.end();
            });
        }
        
        // CACHE MISS
        m_stats.cache_misses.fetch_add(1, std::memory_order_relaxed);
        m_loading_pages.insert(page_id);
        
        // 1. Find a victim frame
        if (!m_free_list.empty()) {
            frame_id = m_free_list.front();
            m_free_list.pop_front();
        } else {
            auto victim_opt = m_replacer->victim();
            if (!victim_opt) {
                m_loading_pages.erase(page_id);
                lock.unlock();
                m_page_table_cv.notify_all();
                return Result<PageHandle>(Status::InternalError);
            }
            frame_id = victim_opt.value();
            m_stats.evictions.fetch_add(1, std::memory_order_relaxed);
        }
        
        // 2. Remove old entry from page table if valid
        auto& frame = m_frames[frame_id.value()];
        if (frame->is_valid()) {
            evicted_page_id = frame->page_id();
            m_page_table.erase(evicted_page_id);
            m_loading_pages.insert(evicted_page_id);
            evicted_page = true;
        }
        
        // 3. Mark the new entry as "in transit" by adding it to the table but leaving 
        // it locked in its frame mutex. Pin it so it can't be evicted.
        frame->pin();
        m_replacer->pin(frame_id);
    } // End page table lock

    // 4. Safely perform disk I/O outside the global page table lock
    auto& frame = m_frames[frame_id.value()];
    std::unique_lock<std::mutex> io_lock(frame->io_mutex());

    // Flush dirty page if needed
    if (frame->is_valid() && frame->is_dirty()) {
        auto write_res = flush_page_wal(m_wal_manager, frame->page());
        if (write_res) write_res = m_disk_manager->write_page(frame->page());
        if (!write_res) {
            // Unpin, cleanup, and return error
            frame->unpin();
            unpin_page(frame_id);
            io_lock.unlock();
            {
                std::lock_guard<std::mutex> lock(m_page_table_mutex);
                if (evicted_page) m_page_table[evicted_page_id] = frame_id;
                m_loading_pages.erase(page_id);
                if (evicted_page) m_loading_pages.erase(evicted_page_id);
            }
            m_page_table_cv.notify_all();
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
        frame->set_valid(false);
        io_lock.unlock();
        {
            std::lock_guard<std::mutex> lock(m_page_table_mutex);
            m_loading_pages.erase(page_id);
            if (evicted_page) m_loading_pages.erase(evicted_page_id);
            m_free_list.push_back(frame_id);
        }
        m_page_table_cv.notify_all();
        return Result<PageHandle>(read_res.error());
    }

    // Move data into frame
    frame->page() = std::move(read_res.value());
    frame->set_page_id(page_id);
    frame->set_valid(true);
    frame->set_dirty(false);
    
    m_replacer->record_access(frame_id);

    io_lock.unlock();
    {
        std::lock_guard<std::mutex> lock(m_page_table_mutex);
        m_page_table[page_id] = frame_id;
        m_loading_pages.erase(page_id);
        if (evicted_page) m_loading_pages.erase(evicted_page_id);
    }
    m_page_table_cv.notify_all();
    return PageHandle(this, frame.get());
}

Result<PageHandle> BufferPool::allocate_page() {
    auto id_res = m_disk_manager->allocate_page();
    if (!id_res) return Result<PageHandle>(id_res.error());
    PageID new_page_id = id_res.value();
    auto handle = fetch_page(new_page_id);
    if (!handle) return handle.error();

    auto& page = handle.value().get_mut();
    std::fill(page.data().begin(), page.data().end(), std::byte{0});
    PageHeader header{};
    header.page_id = new_page_id;
    header.page_type = PageType::Invalid;
    header.payload_size = static_cast<std::uint32_t>(page.payload_span().size());
    page.write_header(header);
    handle.value().mark_dirty();
    return std::move(handle.value());
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
        auto write_res = flush_page_wal(m_wal_manager, frame->page());
        if (write_res) write_res = m_disk_manager->write_page(frame->page());
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
            auto write_res = flush_page_wal(m_wal_manager, frame->page());
            if (write_res) write_res = m_disk_manager->write_page(frame->page());
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

Result<void> BufferPool::write_recovery_page(
    PageID page_id, std::span<const std::byte> page_bytes) {
    if (page_bytes.size() != m_page_size) {
        return Result<void>(Status::InvalidArgument);
    }

    auto handle_result = fetch_page(page_id);
    if (!handle_result) return Result<void>(handle_result.error());

    auto& page = handle_result.value().get_mut();
    std::copy(page_bytes.begin(), page_bytes.end(), page.data().begin());
    auto write_result = m_disk_manager->write_page_from_recovery(page);
    if (!write_result) return write_result;
    page.clear_dirty();
    return Result<void>::success();
}

void BufferPool::discard_dirty_pages() noexcept {
    std::lock_guard<std::mutex> page_lock(m_page_table_mutex);
    for (const auto& [_, frame_id] : m_page_table) {
        auto& frame = m_frames[frame_id.value()];
        std::lock_guard<std::mutex> io_lock(frame->io_mutex());
        frame->set_dirty(false);
    }
}

} // namespace nemo::storage
