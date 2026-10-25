#ifndef NEMO_TRANSACTION_TRANSACTION_HPP
#define NEMO_TRANSACTION_TRANSACTION_HPP

#include "nemo/transaction/transaction_id.hpp"
#include "nemo/transaction/transaction_state.hpp"
#include "nemo/transaction/undo_record.hpp"
#include "nemo/concurrency/lock_resource.hpp"
#include <vector>
#include <mutex>

namespace nemo::transaction {

class Transaction {
public:
    explicit Transaction(TransactionID id, IsolationLevel iso_level = IsolationLevel::ReadCommitted);
    
    TransactionID get_id() const { return m_id; }
    TransactionState get_state() const { return m_state; }
    void set_state(TransactionState state) { m_state = state; }
    IsolationLevel get_isolation_level() const { return m_iso_level; }
    
    void add_undo_record(UndoRecord record);
    const std::vector<UndoRecord>& get_undo_records() const { return m_undo_records; }
    void clear_undo_records(); // Used after successful commit
    
    // Lock Tracking for Module 10
    void add_lock(concurrency::LockResource resource);
    const std::vector<concurrency::LockResource>& get_locks() const { return m_locks; }
    void clear_locks();
    
    // WAL Tracking for Module 11
    std::uint64_t get_last_lsn() const { return m_last_lsn; }
    void set_last_lsn(std::uint64_t lsn) { m_last_lsn = lsn; }
    
private:
    TransactionID m_id;
    TransactionState m_state;
    std::uint64_t m_last_lsn{0};
    IsolationLevel m_iso_level;
    
    // In a full implementation, we'd use a dedicated WriteSet structure, but a vector of UndoRecords serves this for Module 9.
    std::vector<UndoRecord> m_undo_records;
    
    // Track acquired locks
    std::vector<concurrency::LockResource> m_locks;
    
    // Concurrency control for adding writes/locks
    std::mutex m_mutex;
};

} // namespace nemo::transaction

#endif // NEMO_TRANSACTION_TRANSACTION_HPP
