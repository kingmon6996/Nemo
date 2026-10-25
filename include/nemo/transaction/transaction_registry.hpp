#ifndef NEMO_TRANSACTION_TRANSACTION_REGISTRY_HPP
#define NEMO_TRANSACTION_TRANSACTION_REGISTRY_HPP

#include "nemo/transaction/transaction_id.hpp"
#include "nemo/transaction/transaction_state.hpp"
#include <unordered_map>
#include <shared_mutex>
#include <vector>
#include <optional>

namespace nemo::transaction {

// Responsible for tracking all known transactions and their states in-memory.
// Also acts as the "Commit Table" for testing whether old transactions committed or aborted.
class TransactionRegistry {
public:
    void register_transaction(TransactionID id, TransactionState state = TransactionState::Active);
    
    bool update_state(TransactionID id, TransactionState new_state);
    
    std::optional<TransactionState> get_state(TransactionID id) const;
    
    // Returns true if state is committed. False if active, aborted, or unknown.
    bool is_committed(TransactionID id) const;
    
    // Returns true if aborted or unknown (to be safe on very old txns not in WAL yet).
    bool is_aborted(TransactionID id) const;
    
    // Returns a copy of currently active transactions
    std::vector<TransactionID> get_active_transactions() const;
    
    // MVCC GC Foundation
    TransactionID get_oldest_active_transaction() const;
    
private:
    // map of TxnID -> State
    std::unordered_map<TransactionID, TransactionState> m_registry;
    mutable std::shared_mutex m_mutex;
};

} // namespace nemo::transaction

#endif // NEMO_TRANSACTION_TRANSACTION_REGISTRY_HPP
