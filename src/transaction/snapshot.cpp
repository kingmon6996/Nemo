#include "nemo/transaction/snapshot.hpp"

namespace nemo::transaction {

Snapshot::Snapshot(TransactionID xmin, TransactionID xmax, std::vector<TransactionID> active_txns)
    : m_xmin(xmin), m_xmax(xmax), m_active_txns(std::move(active_txns)) {
    std::sort(m_active_txns.begin(), m_active_txns.end());
}

bool Snapshot::is_active(TransactionID txn_id) const {
    // If it's less than xmin, it was committed before this snapshot
    if (txn_id < m_xmin) return false;
    
    // If it's >= xmax, it started after this snapshot was taken
    if (txn_id >= m_xmax) return true;
    
    // Binary search through the active list
    auto it = std::lower_bound(m_active_txns.begin(), m_active_txns.end(), txn_id);
    return it != m_active_txns.end() && *it == txn_id;
}

} // namespace nemo::transaction
