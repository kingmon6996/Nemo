#include "nemo/runtime/connection_manager.hpp"

namespace nemo::runtime {

std::shared_ptr<Connection> ConnectionManager::create_connection() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_shutdown) return nullptr;
    
    std::uint32_t id_val = static_cast<std::uint32_t>(m_next_id++);
    ConnectionID id{id_val};
    
    auto conn = std::make_shared<Connection>(id);
    m_connections[id_val] = conn;
    
    return conn;
}

std::shared_ptr<Connection> ConnectionManager::get_connection(ConnectionID id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_connections.find(static_cast<std::uint32_t>(id.value()));
    if (it != m_connections.end()) {
        return it->second;
    }
    return nullptr;
}

bool ConnectionManager::close_connection(ConnectionID id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_connections.find(static_cast<std::uint32_t>(id.value()));
    if (it != m_connections.end()) {
        it->second->set_state(ConnectionState::CLOSED);
        m_connections.erase(it);
        return true;
    }
    return false;
}

std::size_t ConnectionManager::active_connections_count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_connections.size();
}

void ConnectionManager::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_shutdown = true;
    for (auto& [id, conn] : m_connections) {
        conn->set_state(ConnectionState::CLOSED);
        conn->cancellation_token().cancel();
    }
    m_connections.clear();
}

} // namespace nemo::runtime
