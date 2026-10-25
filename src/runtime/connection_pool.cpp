#include "nemo/runtime/connection_pool.hpp"
#include "nemo/runtime/connection_lease.hpp"

namespace nemo::runtime {

ConnectionPool::ConnectionPool(std::shared_ptr<ConnectionManager> manager, std::size_t max_connections)
    : m_manager(std::move(manager)), m_max_connections(max_connections) {}

std::shared_ptr<ConnectionLease> ConnectionPool::acquire(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(m_mutex);
    
    auto wait_cond = [this] {
        return m_shutdown || !m_idle_connections.empty() || m_active_count < m_max_connections;
    };
    
    if (timeout.count() > 0) {
        if (!m_cv.wait_for(lock, timeout, wait_cond)) {
            return nullptr; // Timeout
        }
    } else {
        m_cv.wait(lock, wait_cond);
    }
    
    if (m_shutdown) return nullptr;
    
    std::shared_ptr<Connection> conn;
    if (!m_idle_connections.empty()) {
        conn = m_idle_connections.front();
        m_idle_connections.pop_front();
    } else {
        conn = m_manager->create_connection();
    }
    
    if (conn) {
        m_active_count++;
        conn->set_state(ConnectionState::BUSY);
        return std::make_shared<ConnectionLease>(shared_from_this(), std::move(conn));
    }
    return nullptr;
}

void ConnectionPool::release(std::shared_ptr<Connection> conn) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_shutdown) {
        m_manager->close_connection(conn->id());
        return;
    }
    
    conn->set_state(ConnectionState::IDLE);
    m_idle_connections.push_back(std::move(conn));
    m_active_count--;
    m_cv.notify_one();
}

void ConnectionPool::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_shutdown = true;
    for (auto& conn : m_idle_connections) {
        m_manager->close_connection(conn->id());
    }
    m_idle_connections.clear();
    m_cv.notify_all();
}

} // namespace nemo::runtime


