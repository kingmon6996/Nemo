#include "nemo/runtime/query_registry.hpp"
#include <algorithm>

namespace nemo::runtime {

QueryID QueryRegistry::register_query(ConnectionID conn_id, const std::string& sql_hash) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::uint64_t id_val = m_next_id++;
    QueryID id{static_cast<std::uint32_t>(id_val)};
    
    QueryInfo info;
    info.id = id;
    info.connection_id = conn_id;
    info.sql_hash = sql_hash;
    info.state = QueryState::CREATED;
    info.start_time = std::chrono::steady_clock::now();
    
    m_queries[id_val] = std::move(info);
    return id;
}

void QueryRegistry::update_state(QueryID query_id, QueryState new_state) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_queries.find(query_id.value());
    if (it != m_queries.end()) {
        it->second.state = new_state;
    }
}

void QueryRegistry::update_memory(QueryID query_id, std::size_t memory_bytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_queries.find(query_id.value());
    if (it != m_queries.end()) {
        it->second.peak_memory_bytes = std::max(it->second.peak_memory_bytes, memory_bytes);
    }
}

void QueryRegistry::unregister_query(QueryID query_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queries.erase(query_id.value());
}

std::vector<QueryInfo> QueryRegistry::get_active_queries() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<QueryInfo> result;
    result.reserve(m_queries.size());
    for (const auto& [id, info] : m_queries) {
        result.push_back(info);
    }
    return result;
}

} // namespace nemo::runtime
