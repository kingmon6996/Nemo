#ifndef NEMO_WAL_TRANSACTION_TABLE_HPP
#define NEMO_WAL_TRANSACTION_TABLE_HPP

#include "nemo/transaction/transaction_id.hpp"
#include "nemo/transaction/transaction_state.hpp"
#include "nemo/wal/lsn.hpp"
#include <unordered_map>
#include <vector>

namespace nemo::wal {

struct TransactionTableEntry {
    transaction::TransactionID txn_id;
    transaction::TransactionState state;
    LSN last_lsn;
    LSN undo_next_lsn; // Used during undo phase for CLRs
};

class RecoveryTransactionTable {
public:
    void update(transaction::TransactionID txn_id, transaction::TransactionState state, LSN last_lsn) {
        auto& entry = m_table[txn_id];
        entry.txn_id = txn_id;
        entry.state = state;
        entry.last_lsn = last_lsn;
        if (!entry.undo_next_lsn.is_valid()) {
            entry.undo_next_lsn = last_lsn;
        }
    }
    
    void remove(transaction::TransactionID txn_id) {
        m_table.erase(txn_id);
    }
    
    TransactionTableEntry* get(transaction::TransactionID txn_id) {
        auto it = m_table.find(txn_id);
        if (it != m_table.end()) return &it->second;
        return nullptr;
    }
    
    // Returns transactions that were active (or aborting) at crash time and need to be undone
    std::vector<TransactionTableEntry> get_losers() const {
        std::vector<TransactionTableEntry> losers;
        for (const auto& [id, entry] : m_table) {
            if (entry.state != transaction::TransactionState::Committed) {
                losers.push_back(entry);
            }
        }
        return losers;
    }

private:
    std::unordered_map<transaction::TransactionID, TransactionTableEntry> m_table;
};

} // namespace nemo::wal

#endif // NEMO_WAL_TRANSACTION_TABLE_HPP
