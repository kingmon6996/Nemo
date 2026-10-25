#ifndef NEMO_WAL_LOG_BUFFER_HPP
#define NEMO_WAL_LOG_BUFFER_HPP

#include "nemo/wal/log_record.hpp"
#include <vector>
#include <cstdint>
#include <mutex>

namespace nemo::wal {

// A thread-safe buffer for batching LogRecords before flushing to disk.
class LogBuffer {
public:
    explicit LogBuffer(std::size_t capacity = 1024 * 1024 * 4) // 4MB default
        : m_capacity(capacity) {
        m_buffer.reserve(m_capacity);
    }

    // Appends a record to the buffer. Returns true if successful, false if the buffer is full.
    bool append(const LogRecord& record) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        std::uint32_t size = record.get_size();
        if (m_buffer.size() + size > m_capacity) {
            return false; // Full
        }
        
        record.serialize(m_buffer);
        return true;
    }

    // Swaps the contents of this buffer with an empty one (used for double-buffering flush)
    void swap(std::vector<std::uint8_t>& empty_buffer) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_buffer.swap(empty_buffer);
    }

    bool is_empty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_buffer.empty();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_buffer.size();
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_buffer.clear();
    }

private:
    std::size_t m_capacity;
    std::vector<std::uint8_t> m_buffer;
    mutable std::mutex m_mutex;
};

} // namespace nemo::wal

#endif // NEMO_WAL_LOG_BUFFER_HPP
