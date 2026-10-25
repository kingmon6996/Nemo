#include "nemo/transaction/transaction_registry.hpp"
#include "nemo/transaction/transaction_registry.hpp"
#include <algorithm>
#include <mutex>

namespace nemo::transaction {

void TransactionRegistry::register_transaction(TransactionID id, TransactionState state) {
    std::unique_lock lock(m_mutex);
    m_registry[id] = state;
}

bool TransactionRegistry::update_state(TransactionID id, TransactionState new_state) {
    std::unique_lock lock(m_mutex);
    auto it = m_registry.find(id);
    if (it != m_registry.end()) {
        it->second = new_state;
        return true;
    }
    return false;
}

std::optional<TransactionState> TransactionRegistry::get_state(TransactionID id) const {
    std::shared_lock lock(m_mutex);
    auto it = m_registry.find(id);
    if (it != m_registry.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool TransactionRegistry::is_committed(TransactionID id) const {
    auto state = get_state(id);
    return state.has_value() && *state == TransactionState::Committed;
}

bool TransactionRegistry::is_aborted(TransactionID id) const {
    auto state = get_state(id);
    return !state.has_value() || *state == TransactionState::Aborted; // Treat unknown as aborted/rolled back (simplification)
}

std::vector<TransactionID> TransactionRegistry::get_active_transactions() const {
    std::shared_lock lock(m_mutex);
    std::vector<TransactionID> active_txns;
    for (const auto& [id, state] : m_registry) {
        if (state == TransactionState::Active || state == TransactionState::Committing || state == TransactionState::Aborting) {
            active_txns.push_back(id);
        }
    }
    return active_txns;
}

TransactionID TransactionRegistry::get_oldest_active_transaction() const {
    std::shared_lock lock(m_mutex);
    TransactionID oldest = std::numeric_limits<TransactionID>::max();
    for (const auto& [id, state] : m_registry) {
        if (state == TransactionState::Active || state == TransactionState::Committing || state == TransactionState::Aborting) {
            if (id < oldest) {
                oldest = id;
            }
        }
    }
    return (oldest == std::numeric_limits<TransactionID>::max()) ? INVALID_TRANSACTION_ID : oldest;
}

} // namespace nemo::transaction
