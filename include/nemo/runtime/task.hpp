#ifndef NEMO_RUNTIME_TASK_HPP
#define NEMO_RUNTIME_TASK_HPP

#include "nemo/runtime/query_registry.hpp"
#include "nemo/runtime/connection.hpp"
#include <chrono>
#include <functional>
#include <optional>

namespace nemo::runtime {

struct QueryOptions {
    std::chrono::milliseconds timeout{0};
};

class QueryTask {
public:
    using ExecutionCallback = std::function<void(QueryTask&)>;

    QueryTask(QueryID q_id, Connection* conn, ExecutionCallback callback)
        : m_query_id(q_id), m_connection(conn), m_callback(std::move(callback)) {}

    QueryID query_id() const noexcept { return m_query_id; }
    Connection* connection() const noexcept { return m_connection; }
    
    // Execute the task. To be called by the Worker.
    void execute() {
        if (m_callback) {
            m_callback(*this);
        }
    }

private:
    QueryID m_query_id;
    Connection* m_connection;
    ExecutionCallback m_callback;
};

} // namespace nemo::runtime

#endif // NEMO_RUNTIME_TASK_HPP
