#ifndef NEMO_RUNTIME_CONNECTION_HPP
#define NEMO_RUNTIME_CONNECTION_HPP

#include "nemo/runtime/session.hpp"
#include "nemo/runtime/cancellation.hpp"
#include "nemo/core/ids.hpp"

namespace nemo::runtime {

struct ConnectionIDTag {};
using ConnectionID = StrongID<ConnectionIDTag>;

enum class ConnectionState {
    OPENING,
    OPEN,
    BUSY,
    IDLE,
    CLOSING,
    CLOSED,
    FAILED
};

class Connection {
public:
    explicit Connection(ConnectionID id) : m_id(id), m_state(ConnectionState::OPEN) {}
    
    ConnectionID id() const noexcept { return m_id; }
    
    ConnectionState state() const noexcept { return m_state; }
    void set_state(ConnectionState state) noexcept { m_state = state; }
    
    SessionContext& session() { return m_session; }
    const SessionContext& session() const { return m_session; }
    
    CancellationToken& cancellation_token() { return m_cancel_token; }
    
    void reset_for_pool() {
        m_session.reset();
        m_cancel_token.reset();
        m_state = ConnectionState::OPEN;
    }

private:
    ConnectionID m_id;
    ConnectionState m_state;
    SessionContext m_session;
    CancellationToken m_cancel_token;
};

} // namespace nemo::runtime

#endif // NEMO_RUNTIME_CONNECTION_HPP
