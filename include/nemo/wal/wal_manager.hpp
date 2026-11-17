#ifndef NEMO_WAL_WAL_MANAGER_HPP
#define NEMO_WAL_WAL_MANAGER_HPP

#include "nemo/wal/lsn.hpp"
#include "nemo/wal/log_record.hpp"
#include "nemo/wal/log_buffer.hpp"
#include "nemo/wal/wal_segment.hpp"
#include "nemo/wal/checkpoint_state.hpp"
#include "nemo/transaction/transaction_id.hpp"
#include <mutex>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <unordered_map>
#include <map>
#include <utility>

namespace nemo::wal {

enum class DurabilityMode {
    Full,     // fsync on every commit
    Normal,   // group commit / batch fsync
    Off       // explicit explicit async flush only
};

// Manages appending to the WAL, buffering, and flushing to segments.
class WALManager {
public:
    static constexpr std::size_t BUFFER_CAPACITY = 4 * 1024 * 1024;
    static constexpr std::chrono::milliseconds GROUP_COMMIT_WINDOW{5};
    static constexpr std::size_t GROUP_COMMIT_SIZE = 4 * 1024 * 1024;
    static constexpr std::uint64_t MAX_SEGMENT_SIZE = 256ULL * 1024 * 1024;

    explicit WALManager(const std::string& wal_directory,
                        DurabilityMode mode = DurabilityMode::Normal,
                        std::uint64_t max_segment_size = MAX_SEGMENT_SIZE);
    ~WALManager();

    class TransactionScope {
    public:
        TransactionScope(WALManager& manager, transaction::TransactionID txn_id);
        ~TransactionScope();
        TransactionScope(const TransactionScope&) = delete;
        TransactionScope& operator=(const TransactionScope&) = delete;
        TransactionScope(TransactionScope&& other) noexcept;
        TransactionScope& operator=(TransactionScope&&) = delete;

    private:
        WALManager* m_manager;
        transaction::TransactionID m_previous_id;
        bool m_had_previous;
    };

    TransactionScope transaction_scope(transaction::TransactionID txn_id);
    transaction::TransactionID current_transaction_id() const noexcept;
    transaction::TransactionID max_transaction_id() const noexcept {
        return m_max_transaction_id;
    }

    // Appends a record to the WAL buffer.
    // Returns the assigned LSN for this record.
    LSN append(LogRecord& record);
    LSN append_checkpoint_end(std::vector<CheckpointDirtyPage> dirty_pages);

    // Ensures all log records up to and including the given LSN are safely flushed to durable storage.
    void flush_up_to(LSN target_lsn);

    // Flushes all currently buffered records.
    void flush();
    void truncate_before(LSN first_required_lsn);
    std::uint64_t wal_size_bytes() const noexcept {
        return m_wal_size_bytes.load(std::memory_order_acquire);
    }
    std::vector<CheckpointTransaction> active_transactions();

    // Returns the highest LSN that is guaranteed to be durable.
    LSN get_flushed_lsn() const { return LSN(m_flushed_lsn.load(std::memory_order_acquire)); }

    DurabilityMode get_durability_mode() const { return m_mode; }
    void set_durability_mode(DurabilityMode mode) { m_mode = mode; }

private:
    friend class WALManagerTestAccess;

    std::string m_wal_directory;
    DurabilityMode m_mode;
    std::uint64_t m_max_segment_size;

    std::atomic<std::uint64_t> m_next_lsn{1};
    std::atomic<std::uint64_t> m_flushed_lsn{0};
    std::atomic<std::uint64_t> m_wal_size_bytes{0};
    std::atomic<bool> m_failed{false};
    
    std::unique_ptr<LogBuffer> m_buffer;
    std::vector<std::uint8_t> m_flush_buffer;

    std::mutex m_append_mutex; // Protects LSN generation and buffer append
    std::mutex m_flush_mutex;  // Protects disk flush operations

    std::unique_ptr<WALSegment> m_current_segment;
    std::uint64_t m_current_segment_id{1};
    std::uint64_t m_buffer_first_lsn{0};
    std::uint64_t m_buffer_last_lsn{0};
    std::uint64_t m_flush_first_lsn{0};
    std::uint64_t m_flush_last_lsn{0};
    std::chrono::steady_clock::time_point m_first_buffered_at{};
    std::unordered_map<transaction::TransactionID, LSN> m_transaction_last_lsn;
    std::unordered_map<transaction::TransactionID, CheckpointTransaction>
        m_active_transactions;
    std::map<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>>
        m_segment_lsn_ranges;
    transaction::TransactionID m_max_transaction_id{0};
    std::function<void()> m_before_segment_append_for_testing;
    std::function<void()> m_before_segment_flush_for_testing;

    static bool should_wait_for_group_commit(std::size_t buffered_bytes) noexcept;
    LSN append_locked(LogRecord& record);
    void roll_segment_if_needed();
    void execute_flush(bool wait_for_batch);
};

} // namespace nemo::wal

#endif // NEMO_WAL_WAL_MANAGER_HPP
