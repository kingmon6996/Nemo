#ifndef NEMO_WAL_WAL_MANAGER_HPP
#define NEMO_WAL_WAL_MANAGER_HPP

#include "nemo/wal/lsn.hpp"
#include "nemo/wal/log_record.hpp"
#include "nemo/wal/log_buffer.hpp"
#include "nemo/wal/wal_segment.hpp"
#include <mutex>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace nemo::wal {

enum class DurabilityMode {
    Full,     // fsync on every commit
    Normal,   // group commit / batch fsync
    Off       // explicit explicit async flush only
};

// Manages appending to the WAL, buffering, and flushing to segments.
class WALManager {
public:
    explicit WALManager(const std::string& wal_directory, DurabilityMode mode = DurabilityMode::Normal);
    ~WALManager();

    // Appends a record to the WAL buffer. 
    // Returns the assigned LSN for this record.
    LSN append(LogRecord& record);
    
    // Ensures all log records up to and including the given LSN are safely flushed to durable storage.
    void flush_up_to(LSN target_lsn);
    
    // Flushes all currently buffered records.
    void flush();
    
    // Returns the highest LSN that is guaranteed to be durable.
    LSN get_flushed_lsn() const { return LSN(m_flushed_lsn.load(std::memory_order_acquire)); }
    
    DurabilityMode get_durability_mode() const { return m_mode; }
    void set_durability_mode(DurabilityMode mode) { m_mode = mode; }

private:
    std::string m_wal_directory;
    DurabilityMode m_mode;
    
    std::atomic<std::uint64_t> m_next_lsn{1};
    std::atomic<std::uint64_t> m_flushed_lsn{0};
    
    std::unique_ptr<LogBuffer> m_buffer;
    std::vector<std::uint8_t> m_flush_buffer; // Used during double-buffering flush
    
    std::mutex m_append_mutex; // Protects LSN generation and buffer append
    std::mutex m_flush_mutex;  // Protects disk flush operations
    
    std::unique_ptr<WALSegment> m_current_segment;
    std::uint64_t m_current_segment_id{1};
    
    static constexpr std::uint64_t MAX_SEGMENT_SIZE = 16 * 1024 * 1024; // 16 MB

    void roll_segment_if_needed();
    void execute_flush();
};

} // namespace nemo::wal

#endif // NEMO_WAL_WAL_MANAGER_HPP
