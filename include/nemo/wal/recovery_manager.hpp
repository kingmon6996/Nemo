#ifndef NEMO_WAL_RECOVERY_MANAGER_HPP
#define NEMO_WAL_RECOVERY_MANAGER_HPP

#include "nemo/wal/wal_manager.hpp"
#include "nemo/wal/checkpoint_manager.hpp"
#include "nemo/wal/recovery_state.hpp"
#include "nemo/wal/transaction_table.hpp"
#include "nemo/wal/dirty_page_table.hpp"
#include "nemo/storage/buffer_pool.hpp"
#include <string>

namespace nemo::wal {

class RecoveryManager {
public:
    RecoveryManager(WALManager* wal, CheckpointManager* checkpoint, storage::BufferPool* buffer_pool, const std::string& wal_dir);
    
    // Executes the entire recovery pipeline (Analysis -> Redo -> Undo)
    bool recover();
    
    RecoveryState get_state() const { return m_state; }

private:
    WALManager* m_wal_manager;
    CheckpointManager* m_checkpoint_manager;
    storage::BufferPool* m_buffer_pool;
    std::string m_wal_dir;
    
    RecoveryState m_state{RecoveryState::NotRequired};
    
    RecoveryTransactionTable m_txn_table;
    DirtyPageTable m_dirty_page_table;
    
    LSN m_redo_start_lsn{LSN::invalid()};

    bool phase_analysis();
    bool phase_redo();
    bool phase_undo();
    
    void redo_record(const LogRecord& record);
    void undo_record(const LogRecord& record);
};

} // namespace nemo::wal

#endif // NEMO_WAL_RECOVERY_MANAGER_HPP
