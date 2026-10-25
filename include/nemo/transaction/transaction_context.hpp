#ifndef NEMO_TRANSACTION_TRANSACTION_CONTEXT_HPP
#define NEMO_TRANSACTION_TRANSACTION_CONTEXT_HPP

#include "nemo/transaction/transaction.hpp"
#include "nemo/transaction/snapshot.hpp"
#include <optional>

namespace nemo::transaction {

// Used by execution and storage layers to pass transaction state around securely
class TransactionContext {
public:
    TransactionContext(Transaction* txn, Snapshot current_snapshot)
        : m_txn(txn), m_snapshot(std::move(current_snapshot)) {}
        
    Transaction* get_transaction() const { return m_txn; }
    const Snapshot& get_snapshot() const { return m_snapshot; }
    
    // Allow updating the snapshot (e.g. for ReadCommitted between statements)
    void update_snapshot(Snapshot new_snapshot) {
        m_snapshot = std::move(new_snapshot);
    }
    
private:
    Transaction* m_txn;
    Snapshot m_snapshot;
};

} // namespace nemo::transaction

#endif // NEMO_TRANSACTION_TRANSACTION_CONTEXT_HPP
