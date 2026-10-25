#ifndef NEMO_RUNTIME_CONNECTION_POOL_HPP
#define NEMO_RUNTIME_CONNECTION_POOL_HPP

#include "nemo/runtime/connection_manager.hpp"
#include <mutex>
#include <condition_variable>
#include <deque>
#include <chrono>

namespace nemo::runtime {

class ConnectionLease;

class ConnectionPool : public std::enable_shared_from_this<ConnectionPool> {
public:
    ConnectionPool(std::shared_ptr<ConnectionManager> manager, std::size_t max_connections);
    
    // Block until a connection is available or timeout occurs
    std::shared_ptr<ConnectionLease> acquire(std::chrono::milliseconds timeout = std::chrono::milliseconds(0));
    
    // Internal method to return a connection to the pool
    void release(std::shared_ptr<Connection> conn);

    void shutdown();

private:
    std::shared_ptr<ConnectionManager> m_manager;
    std::size_t m_max_connections;
    
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::shared_ptr<Connection>> m_idle_connections;
    std::size_t m_active_count{0};
    bool m_shutdown{false};
};

} // namespace nemo::runtime

#endif // NEMO_RUNTIME_CONNECTION_POOL_HPP

