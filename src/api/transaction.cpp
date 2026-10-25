#include "nemo/api/transaction.hpp"
#include "nemo/api/connection.hpp"

namespace nemo::api {

Transaction::Transaction(Connection* conn) : m_conn(conn), m_active(true) {}

Transaction::~Transaction() {
    if (m_active && m_conn) {
        // Ignore failure
        (void)m_conn->rollback();
    }
}

Transaction::Transaction(Transaction&& other) noexcept 
    : m_conn(other.m_conn), m_active(other.m_active) {
    other.m_active = false;
}

Transaction& Transaction::operator=(Transaction&& other) noexcept {
    if (this != &other) {
        if (m_active && m_conn) {
            (void)m_conn->rollback();
        }
        m_conn = other.m_conn;
        m_active = other.m_active;
        other.m_active = false;
    }
    return *this;
}

nemo::Result<void> Transaction::commit() {
    if (!m_active || !m_conn) return Status::InvalidState;
    m_active = false;
    return m_conn->commit();
}

nemo::Result<void> Transaction::rollback() {
    if (!m_active || !m_conn) return Status::InvalidState;
    m_active = false;
    return m_conn->rollback();
}

nemo::Result<void> Transaction::savepoint(std::string_view /*name*/) {
    return Status::Unsupported;
}

nemo::Result<void> Transaction::rollback_to(std::string_view /*name*/) {
    return Status::Unsupported;
}

nemo::Result<void> Transaction::release_savepoint(std::string_view /*name*/) {
    return Status::Unsupported;
}

} // namespace nemo::api
