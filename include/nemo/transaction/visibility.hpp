#ifndef NEMO_TRANSACTION_VISIBILITY_HPP
#define NEMO_TRANSACTION_VISIBILITY_HPP

#include "nemo/transaction/snapshot.hpp"
#include "nemo/transaction/mvcc_header.hpp"
#include "nemo/transaction/transaction_registry.hpp"

namespace nemo::transaction {

class VisibilityManager {
public:
    explicit VisibilityManager(TransactionRegistry& registry);
    
    // Core MVCC visibility rule evaluating if a physical version is logically visible
    // to the given snapshot and current transaction context.
    bool is_visible(const MVCCHeader& header, const Snapshot& snapshot, TransactionID current_txn) const;
    
private:
    TransactionRegistry& m_registry;
};

} // namespace nemo::transaction

#endif // NEMO_TRANSACTION_VISIBILITY_HPP
