#include "nemo/concurrency/wait_for_graph.hpp"
#include <algorithm>

namespace nemo::concurrency {

void WaitForGraph::add_edge(transaction::TransactionID waiting, transaction::TransactionID holding) {
    if (waiting == holding) return; // Prevent self-loops
    std::lock_guard<std::mutex> lock(m_mutex);
    m_edges[waiting].insert(holding);
}

void WaitForGraph::remove_edge(transaction::TransactionID waiting, transaction::TransactionID holding) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_edges.find(waiting);
    if (it != m_edges.end()) {
        it->second.erase(holding);
        if (it->second.empty()) {
            m_edges.erase(it);
        }
    }
}

void WaitForGraph::remove_transaction(transaction::TransactionID txn) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_edges.erase(txn); // Remove edges where txn is waiting
    
    // Remove edges where txn is holding
    for (auto it = m_edges.begin(); it != m_edges.end(); ) {
        it->second.erase(txn);
        if (it->second.empty()) {
            it = m_edges.erase(it);
        } else {
            ++it;
        }
    }
}

std::optional<transaction::TransactionID> WaitForGraph::detect_cycle() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::unordered_set<transaction::TransactionID> visited;
    std::unordered_set<transaction::TransactionID> stack;
    std::vector<transaction::TransactionID> cycle_path;
    
    for (const auto& [node, _] : m_edges) {
        if (visited.find(node) == visited.end()) {
            cycle_path.clear();
            if (dfs_visit(node, visited, stack, cycle_path)) {
                // Cycle found.
                // Victim selection policy: Youngest transaction (largest ID)
                transaction::TransactionID victim = cycle_path.front();
                for (auto txn : cycle_path) {
                    if (txn > victim) {
                        victim = txn;
                    }
                }
                return victim;
            }
        }
    }
    
    return std::nullopt;
}

bool WaitForGraph::dfs_visit(transaction::TransactionID node,
                             std::unordered_set<transaction::TransactionID>& visited,
                             std::unordered_set<transaction::TransactionID>& stack,
                             std::vector<transaction::TransactionID>& cycle_path) {
    visited.insert(node);
    stack.insert(node);
    cycle_path.push_back(node);
    
    auto it = m_edges.find(node);
    if (it != m_edges.end()) {
        for (auto neighbor : it->second) {
            if (stack.find(neighbor) != stack.end()) {
                cycle_path.push_back(neighbor); // Complete the cycle path
                return true;
            }
            if (visited.find(neighbor) == visited.end()) {
                if (dfs_visit(neighbor, visited, stack, cycle_path)) {
                    return true;
                }
            }
        }
    }
    
    stack.erase(node);
    cycle_path.pop_back();
    return false;
}

} // namespace nemo::concurrency
