#ifndef NEMO_WAL_CHECKPOINT_MANAGER_HPP
#define NEMO_WAL_CHECKPOINT_MANAGER_HPP

#include "nemo/wal/wal_manager.hpp"
#include "nemo/wal/checkpoint_state.hpp"
#include "nemo/transaction/transaction_manager.hpp"
#include "nemo/storage/buffer_pool.hpp"
#include <chrono>
#include <mutex>
#include <string>

namespace nemo::wal {

struct CheckpointMetadata {
    LSN checkpoint_lsn;
    std::uint64_t timestamp;
    bool legacy{false};
};

// Orchestrates fuzzy checkpoint creation and the master record.
class CheckpointManager {
public:
    CheckpointManager(WALManager* wal,
                      transaction::TransactionManager* txn_mgr,
                      storage::BufferPool* buffer_pool,
                      const std::string& master_record_path,
                      const std::string& wal_dir);
    
    // Perform a fuzzy checkpoint
    bool perform_checkpoint();
    
    // Reads the master record to find the latest valid checkpoint
    std::optional<CheckpointMetadata> read_master_record();
    std::optional<CheckpointState> read_checkpoint_state(LSN begin_lsn) const;
    bool has_master_record() const;
    bool checkpoint_due() const;

private:
    WALManager* m_wal_manager;
    transaction::TransactionManager* m_txn_manager;
    storage::BufferPool* m_buffer_pool;
    std::string m_master_record_path;
    std::string m_wal_directory;
    mutable std::mutex m_checkpoint_mutex;
    std::chrono::steady_clock::time_point m_last_checkpoint_at;
    std::uint64_t m_last_checkpoint_wal_bytes{0};
    
    bool write_master_record(const CheckpointMetadata& metadata);
};

} // namespace nemo::wal

#endif // NEMO_WAL_CHECKPOINT_MANAGER_HPP
