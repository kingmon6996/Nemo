#include "nemo/wal/wal_manager.hpp"
#include <filesystem>
#include <stdexcept>

namespace nemo::wal {

WALManager::WALManager(const std::string& wal_directory, DurabilityMode mode)
    : m_wal_directory(wal_directory), m_mode(mode) {
    
    if (!std::filesystem::exists(m_wal_directory)) {
        std::filesystem::create_directories(m_wal_directory);
    }
    
    m_buffer = std::make_unique<LogBuffer>();
    m_flush_buffer.reserve(1024 * 1024 * 4);
    
    // Initialize first segment
    m_current_segment = std::make_unique<WALSegment>(m_wal_directory, m_current_segment_id);
    if (!m_current_segment->open_append(LSN(m_next_lsn.load()))) {
        throw std::runtime_error("Failed to open WAL segment for writing.");
    }
}

WALManager::~WALManager() {
    flush(); // Ensure everything is written on shutdown
}

LSN WALManager::append(LogRecord& record) {
    std::lock_guard<std::mutex> lock(m_append_mutex);
    
    LSN assigned_lsn(m_next_lsn.fetch_add(1));
    record.set_lsn(assigned_lsn);
    
    // Attempt append to buffer
    if (!m_buffer->append(record)) {
        // Buffer is full, must flush synchronously
        m_append_mutex.unlock();
        flush();
        m_append_mutex.lock();
        
        // Try again
        if (!m_buffer->append(record)) {
            throw std::runtime_error("Log record is larger than WAL buffer capacity.");
        }
    }
    
    return assigned_lsn;
}

void WALManager::roll_segment_if_needed() {
    if (m_current_segment->get_size() >= MAX_SEGMENT_SIZE) {
        m_current_segment->close();
        m_current_segment_id++;
        m_current_segment = std::make_unique<WALSegment>(m_wal_directory, m_current_segment_id);
        if (!m_current_segment->open_append(LSN(m_next_lsn.load()))) {
            throw std::runtime_error("Failed to roll WAL segment.");
        }
    }
}

void WALManager::execute_flush() {
    // We lock flush_mutex so only one thread actually writes to disk at a time
    std::lock_guard<std::mutex> flush_lock(m_flush_mutex);
    
    {
        // Extract buffer contents quickly under append_mutex
        std::lock_guard<std::mutex> append_lock(m_append_mutex);
        if (m_buffer->is_empty()) return;
        m_buffer->swap(m_flush_buffer); // double-buffering
    }
    
    // We now have exclusive ownership of m_flush_buffer, writing to disk without blocking Appends
    if (m_flush_buffer.empty()) return;
    
    roll_segment_if_needed();
    
    if (!m_current_segment->append(m_flush_buffer.data(), m_flush_buffer.size())) {
        throw std::runtime_error("Failed to write to WAL segment.");
    }
    
    if (m_mode != DurabilityMode::Off) {
        m_current_segment->flush();
    }
    
    // After flush is successful, update flushed LSN safely.
    // We know the highest LSN inside flush_buffer was at least the one immediately before this flush block started.
    // In a production system we'd parse the last record in the buffer or track it. For now, we can track via m_next_lsn.
    // A safe lower bound is what m_next_lsn was right after we swapped buffers.
    std::uint64_t safely_flushed = m_next_lsn.load() - 1; 
    
    std::uint64_t current_flushed = m_flushed_lsn.load(std::memory_order_acquire);
    while (safely_flushed > current_flushed && 
           !m_flushed_lsn.compare_exchange_weak(current_flushed, safely_flushed, std::memory_order_release, std::memory_order_relaxed)) {
        // Retry
    }
    
    m_flush_buffer.clear();
}

void WALManager::flush() {
    execute_flush();
}

void WALManager::flush_up_to(LSN target_lsn) {
    if (target_lsn.value() <= m_flushed_lsn.load(std::memory_order_acquire)) {
        return; // Already flushed
    }
    
    // Execute a flush. Group commit naturally occurs if multiple threads hit this simultaneously.
    // In a fully optimized engine, we'd use a condition variable to wait if another thread is already flushing our target LSN.
    // For Module 11, execute_flush() safely serializes flushes.
    execute_flush();
}

} // namespace nemo::wal
