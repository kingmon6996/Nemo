#ifndef NEMO_CONCURRENCY_WAIT_FOR_GRAPH_HPP
#define NEMO_CONCURRENCY_WAIT_FOR_GRAPH_HPP

#include "nemo/transaction/transaction_id.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <optional>

namespace nemo::concurrency {

// Centralized wait-for graph for deadlock detection
class WaitForGraph {
public:
    void add_edge(transaction::TransactionID waiting, transaction::TransactionID holding);
    void remove_edge(transaction::TransactionID waiting, transaction::TransactionID holding);
    
    // Removes all wait-for edges where 'txn' is either the waiter or the holder
    void remove_transaction(transaction::TransactionID txn);
    
    // DFS Cycle Detection. Returns the victim TransactionID if a cycle is found.
    std::optional<transaction::TransactionID> detect_cycle();

private:
    // waiting -> { holding }
    std::unordered_map<transaction::TransactionID, std::unordered_set<transaction::TransactionID>> m_edges;
    std::mutex m_mutex;
    
    bool dfs_visit(transaction::TransactionID node,
                   std::unordered_set<transaction::TransactionID>& visited,
                   std::unordered_set<transaction::TransactionID>& stack,
                   std::vector<transaction::TransactionID>& cycle_path);
};

} // namespace nemo::concurrency

#endif // NEMO_CONCURRENCY_WAIT_FOR_GRAPH_HPP
