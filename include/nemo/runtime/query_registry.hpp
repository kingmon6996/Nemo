#ifndef NEMO_RUNTIME_QUERY_REGISTRY_HPP
#define NEMO_RUNTIME_QUERY_REGISTRY_HPP

#include "nemo/core/ids.hpp"
#include "nemo/runtime/connection.hpp"
#include <mutex>
#include <unordered_map>
#include <chrono>

namespace nemo::runtime {

struct QueryIDTag {};
using QueryID = StrongID<QueryIDTag>;

enum class QueryState {
    CREATED,
    QUEUED,
    RUNNING,
    COMPLETED,
    FAILED,
    CANCELLED
};

struct QueryInfo {
    QueryID id;
    ConnectionID connection_id;
    std::string sql_hash; // or truncated sql
    QueryState state{QueryState::CREATED};
    std::chrono::steady_clock::time_point start_time;
    std::size_t peak_memory_bytes{0};
};

class QueryRegistry {
public:
    QueryRegistry() = default;

    QueryID register_query(ConnectionID conn_id, const std::string& sql_hash);
    void update_state(QueryID query_id, QueryState new_state);
    void update_memory(QueryID query_id, std::size_t memory_bytes);
    void unregister_query(QueryID query_id);
    
    std::vector<QueryInfo> get_active_queries() const;

private:
    mutable std::mutex m_mutex;
    std::uint64_t m_next_id{1};
    std::unordered_map<std::uint64_t, QueryInfo> m_queries;
};

} // namespace nemo::runtime

#endif // NEMO_RUNTIME_QUERY_REGISTRY_HPP
