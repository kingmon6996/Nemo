#ifndef NEMO_TRANSACTION_SNAPSHOT_HPP
#define NEMO_TRANSACTION_SNAPSHOT_HPP

#include "nemo/transaction/transaction_id.hpp"
#include <unordered_set>
#include <vector>
#include <algorithm>

namespace nemo::transaction {

class Snapshot {
public:
    Snapshot() = default;
    Snapshot(TransactionID xmin, TransactionID xmax, std::vector<TransactionID> active_txns);

    TransactionID get_xmin() const { return m_xmin; }
    TransactionID get_xmax() const { return m_xmax; }
    
    // Returns true if txn_id is considered active in this snapshot
    bool is_active(TransactionID txn_id) const;

private:
    TransactionID m_xmin{INVALID_TRANSACTION_ID};
    TransactionID m_xmax{INVALID_TRANSACTION_ID};
    
    // Optimization: we could use a bitmap or sorted vector. Using sorted vector for cache locality + binary search.
    std::vector<TransactionID> m_active_txns;
};

} // namespace nemo::transaction

#endif // NEMO_TRANSACTION_SNAPSHOT_HPP
