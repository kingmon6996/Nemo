#ifndef NEMO_API_TRANSACTION_HPP
#define NEMO_API_TRANSACTION_HPP

#include "nemo/core/result.hpp"
#include <memory>
#include <string_view>

namespace nemo::api {

class Connection;

class Transaction {
public:
    // Destructor rolls back if not explicitly committed
    ~Transaction();

    Transaction(Transaction&&) noexcept;
    Transaction& operator=(Transaction&&) noexcept;
    
    // Non-copyable
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    nemo::Result<void> commit();
    nemo::Result<void> rollback();

    nemo::Result<void> savepoint(std::string_view name);
    nemo::Result<void> rollback_to(std::string_view name);
    nemo::Result<void> release_savepoint(std::string_view name);

private:
    friend class Connection;
    explicit Transaction(Connection* conn);
    
    Connection* m_conn;
    bool m_active;
};

} // namespace nemo::api

#endif // NEMO_API_TRANSACTION_HPP
