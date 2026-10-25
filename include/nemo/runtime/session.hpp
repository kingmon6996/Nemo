#ifndef NEMO_RUNTIME_SESSION_HPP
#define NEMO_RUNTIME_SESSION_HPP

#include "nemo/transaction/transaction_context.hpp"
#include <memory>
#include <optional>
#include <chrono>
#include <string>
#include <unordered_map>

namespace nemo::runtime {

enum class IsolationLevel {
    ReadCommitted,
    RepeatableRead,
    Serializable
};

struct SessionContext {
    // Transaction state
    std::unique_ptr<transaction::TransactionContext> current_transaction;
    
    // Session settings
    IsolationLevel default_isolation_level{IsolationLevel::ReadCommitted};
    bool autocommit{true};
    bool read_only{false};
    
    std::chrono::milliseconds query_timeout{0}; // 0 = no timeout
    std::size_t memory_limit_bytes{256 * 1024 * 1024}; // 256 MB default
    
    std::string current_schema{"public"};
    
    // Session variables (like SET work_mem = '64MB')
    std::unordered_map<std::string, std::string> session_vars;
    
    // Method to clear/reset the session state for reuse in a pool
    void reset() {
        current_transaction.reset();
        autocommit = true;
        read_only = false;
        default_isolation_level = IsolationLevel::ReadCommitted;
        query_timeout = std::chrono::milliseconds{0};
        current_schema = "public";
        session_vars.clear();
    }
};

} // namespace nemo::runtime

#endif // NEMO_RUNTIME_SESSION_HPP
