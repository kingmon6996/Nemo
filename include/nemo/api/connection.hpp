#ifndef NEMO_API_CONNECTION_HPP
#define NEMO_API_CONNECTION_HPP

#include "nemo/core/result.hpp"
#include "nemo/api/result.hpp"
#include "nemo/api/transaction.hpp"
#include "nemo/api/prepared_statement.hpp"
#include <span>
#include <memory>
#include <string_view>

namespace nemo { class Database; }

namespace nemo::api {

struct ConnectionConfig {
    bool read_only{false};
};

class ConnectionImpl;

class Connection {
public:
    Connection(Connection&&) noexcept;
    Connection& operator=(Connection&&) noexcept;
    ~Connection();

    // Non-copyable
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Execute ad-hoc SQL
    nemo::Result<api::Result> execute(std::string_view sql);
    
    // Execute SQL with parameters
    nemo::Result<api::Result> execute(std::string_view sql, std::span<const types::Value> parameters);

    // Prepare a statement for reuse
    nemo::Result<PreparedStatement> prepare(std::string_view sql);

    // Transaction management
    nemo::Result<Transaction> begin();
    nemo::Result<Transaction> transaction(); // RAII guard
    
    nemo::Result<void> commit();
    nemo::Result<void> rollback();

    // Set autocommit mode
    void set_autocommit(bool enabled);

    // Cancel currently executing query
    void cancel();
    
    // Allocate a contiguous range of IDs from a sequence
    nemo::Result<std::pair<std::int64_t, std::int64_t>> allocate_id_range(std::string_view sequence_name, std::size_t count = 1);

    // Close connection manually (also happens on destruction)
    void close();

    // Internal constructor and impl accessor
    explicit Connection(std::unique_ptr<ConnectionImpl> impl);
    ConnectionImpl* get_impl() noexcept { return m_impl.get(); }
    const ConnectionImpl* get_impl() const noexcept { return m_impl.get(); }
    
private:
    friend class nemo::Database;
    std::unique_ptr<ConnectionImpl> m_impl;
};

} // namespace nemo::api

#endif // NEMO_API_CONNECTION_HPP
