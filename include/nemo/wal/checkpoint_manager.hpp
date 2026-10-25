#ifndef NEMO_WAL_CHECKPOINT_MANAGER_HPP
#define NEMO_WAL_CHECKPOINT_MANAGER_HPP

#include "nemo/wal/wal_manager.hpp"
#include "nemo/transaction/transaction_manager.hpp"
#include "nemo/storage/buffer_pool.hpp"
#include <string>

namespace nemo::wal {

struct CheckpointMetadata {
    LSN checkpoint_lsn;
    std::uint64_t timestamp;
};

// Orchestrates fuzzy checkpoint creation and the master record.
class CheckpointManager {
public:
    CheckpointManager(WALManager* wal, transaction::TransactionManager* txn_mgr, storage::BufferPool* buffer_pool, const std::string& db_dir);
    
    // Perform a fuzzy checkpoint
    bool perform_checkpoint();
    
    // Reads the master record to find the latest valid checkpoint
    std::optional<CheckpointMetadata> read_master_record();

private:
    WALManager* m_wal_manager;
    transaction::TransactionManager* m_txn_manager;
    storage::BufferPool* m_buffer_pool;
    std::string m_master_record_path;
    
    bool write_master_record(const CheckpointMetadata& metadata);
};

} // namespace nemo::wal

#endif // NEMO_WAL_CHECKPOINT_MANAGER_HPP
