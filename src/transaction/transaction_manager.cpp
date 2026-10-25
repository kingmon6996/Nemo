#include "nemo/transaction/transaction_manager.hpp"
#include "nemo/concurrency/lock_manager.hpp"
#include "nemo/wal/wal_manager.hpp"

namespace nemo::transaction {

TransactionManager::TransactionManager(concurrency::LockManager* lock_manager, wal::WALManager* wal_manager) 
    : m_lock_manager(lock_manager), m_wal_manager(wal_manager) {
    // In Module 11 (WAL), we would recover m_next_txn_id from the log.
}

Transaction* TransactionManager::begin(IsolationLevel iso_level) {
    TransactionID txn_id = m_next_txn_id.fetch_add(1, std::memory_order_relaxed);
    
    auto txn = std::make_unique<Transaction>(txn_id, iso_level);
    
    m_registry.register_transaction(txn->get_id());
    
    Transaction* raw_ptr = txn.get();
    
    {
        std::unique_lock<std::shared_mutex> lock(m_txn_mutex);
        m_transactions[txn_id] = std::move(txn);
    }
    
    // Log BEGIN
    if (m_wal_manager) {
        wal::LogRecord begin_record(txn_id, wal::LogRecordType::TxnBegin, wal::LSN::invalid());
        wal::LSN lsn = m_wal_manager->append(begin_record);
        raw_ptr->set_last_lsn(lsn.value()); // Assume Transaction class tracks last_lsn
    }
    
    return raw_ptr;
}

Status TransactionManager::commit(Transaction* txn) {
    if (!txn || txn->get_state() != TransactionState::Active) {
        return Status::InvalidState;
    }
    
    // WAL logging COMMIT
    if (m_wal_manager) {
        wal::LogRecord commit_record(txn->get_id(), wal::LogRecordType::TxnCommit, wal::LSN(txn->get_last_lsn()));
        wal::LSN lsn = m_wal_manager->append(commit_record);
        
        // Wait for durability boundary before returning to client (DurabilityMode FULL/NORMAL depends on flush)
        m_wal_manager->flush_up_to(lsn);
    }
    
    // Complete commit
    m_registry.update_state(txn->get_id(), TransactionState::Committed);
    txn->set_state(TransactionState::Committed);
    
    if (m_lock_manager) {
        m_lock_manager->release_all(txn->get_id(), txn->get_locks());
    }
    
    // We can clear undo records since the transaction is committed
    txn->clear_undo_records();
    txn->clear_locks();
    
    return Status::OK;
}

Status TransactionManager::abort(Transaction* txn) {
    if (!txn || txn->get_state() != TransactionState::Active) {
        return Status::InvalidState;
    }
    
    // Log ABORT
    if (m_wal_manager) {
        wal::LogRecord abort_record(txn->get_id(), wal::LogRecordType::TxnAbort, wal::LSN(txn->get_last_lsn()));
        wal::LSN lsn = m_wal_manager->append(abort_record);
        txn->set_last_lsn(lsn.value());
    }
    
    rollback_undo_records(txn);
    
    // Log END after rollback
    if (m_wal_manager) {
        wal::LogRecord end_record(txn->get_id(), wal::LogRecordType::TxnEnd, wal::LSN(txn->get_last_lsn()));
        m_wal_manager->append(end_record);
        m_wal_manager->flush(); // Optionally group commit this
    }
    
    // Complete abort
    m_registry.update_state(txn->get_id(), TransactionState::Aborted);
    
    // Rollback changes
    rollback_undo_records(txn);
    
    // Complete abort
    m_registry.update_state(txn->get_id(), TransactionState::Aborted);
    txn->set_state(TransactionState::Aborted);
    
    if (m_lock_manager) {
        m_lock_manager->release_all(txn->get_id(), txn->get_locks());
    }
    
    // Once rolled back, clear the records
    txn->clear_undo_records();
    txn->clear_locks();
    
    return Status::OK;
}

void TransactionManager::rollback_undo_records(Transaction* /*txn*/) {
    // This is conceptually where we undo writes.
    // For Insert: we might mark xmax as aborted, or physically reclaim.
    // For Update: we must revert xmax of the old version so it becomes visible again.
    // For Delete: we revert xmax of the deleted version to INVALID so it becomes visible again.
    
    // Since TableHeap physically owns the rows, we will eventually need to callback to storage.
    // The exact physical undo implementation depends heavily on the TableHeap layout.
    // (This forms the boundary for Module 9 / Storage Integration).
}

Snapshot TransactionManager::create_snapshot(const Transaction* /*txn*/) {
    // For ReadCommitted, we get a fresh snapshot showing what's active *right now*.
    // For RepeatableRead, we would ideally cache the snapshot on the Transaction object when it first reads,
    // and return that cached one. For simplicity in Module 9 API, we'll just build it fresh here.
    
    TransactionID xmax = m_next_txn_id.load(std::memory_order_relaxed);
    TransactionID xmin = m_registry.get_oldest_active_transaction();
    if (xmin == INVALID_TRANSACTION_ID || xmin > xmax) {
        xmin = xmax; // No active transactions
    }
    
    std::vector<TransactionID> active_txns = m_registry.get_active_transactions();
    
    return Snapshot(xmin, xmax, std::move(active_txns));
}

} // namespace nemo::transaction
