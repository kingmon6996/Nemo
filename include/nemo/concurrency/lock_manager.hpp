#ifndef NEMO_CONCURRENCY_LOCK_MANAGER_HPP
#define NEMO_CONCURRENCY_LOCK_MANAGER_HPP

#include "nemo/concurrency/lock_resource.hpp"
#include "nemo/concurrency/lock_mode.hpp"
#include "nemo/concurrency/wait_for_graph.hpp"
#include "nemo/core/status.hpp"
#include "nemo/core/result.hpp"
#include <vector>
#include <deque>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <chrono>

namespace nemo::concurrency {

struct LockRequest {
    transaction::TransactionID txn_id;
    LockMode mode;
    bool granted{false};
    bool aborted{false};
};

struct LockState {
    std::unordered_map<transaction::TransactionID, LockMode> holders; // To track modes/ownership per txn
    std::deque<LockRequest> waiters;
    std::condition_variable cv;
};

struct LockTableShard {
    std::mutex mutex;
    std::unordered_map<LockResource, LockState> table;
};

class LockManager {
public:
    explicit LockManager(std::size_t num_shards = 64);
    
    // Core Locking
    Result<void> lock(transaction::TransactionID txn_id, LockResource resource, LockMode mode);
    
    // Timeout variant
    Result<void> lock_with_timeout(transaction::TransactionID txn_id, LockResource resource, LockMode mode, std::chrono::milliseconds timeout);
    
    Result<void> unlock(transaction::TransactionID txn_id, LockResource resource);
    
    // Rollback or Commit cleanup
    void release_all(transaction::TransactionID txn_id, const std::vector<LockResource>& resources);

private:
    std::vector<std::unique_ptr<LockTableShard>> m_shards;
    std::size_t m_num_shards;
    WaitForGraph m_wait_for_graph;
    
    LockTableShard& get_shard(const LockResource& resource);
    
    bool can_grant(const LockState& state, LockMode requested_mode);
    bool check_upgrade(LockState& state, transaction::TransactionID txn_id, LockMode new_mode);
};

} // namespace nemo::concurrency

#endif // NEMO_CONCURRENCY_LOCK_MANAGER_HPP
