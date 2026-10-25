#ifndef NEMO_RUNTIME_CONNECTION_MANAGER_HPP
#define NEMO_RUNTIME_CONNECTION_MANAGER_HPP

#include "nemo/runtime/connection.hpp"
#include <mutex>
#include <unordered_map>
#include <memory>
#include <vector>

namespace nemo::runtime {

class ConnectionManager {
public:
    ConnectionManager() = default;
    
    // Creates a new independent connection (not pooled)
    std::shared_ptr<Connection> create_connection();
    
    // Lookup an active connection
    std::shared_ptr<Connection> get_connection(ConnectionID id);
    
    // Close and remove a connection
    bool close_connection(ConnectionID id);
    
    // Returns total active connections tracked
    std::size_t active_connections_count() const;
    
    // Stop accepting new connections and close existing ones
    void shutdown();

private:
    mutable std::mutex m_mutex;
    std::uint64_t m_next_id{1};
    std::unordered_map<std::uint32_t, std::shared_ptr<Connection>> m_connections;
    bool m_shutdown{false};
};

} // namespace nemo::runtime

#endif // NEMO_RUNTIME_CONNECTION_MANAGER_HPP
