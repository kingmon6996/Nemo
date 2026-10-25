#include "nemo/transaction/transaction.hpp"

namespace nemo::transaction {

Transaction::Transaction(TransactionID id, IsolationLevel iso_level)
    : m_id(id), m_state(TransactionState::Active), m_iso_level(iso_level) {}

void Transaction::add_undo_record(UndoRecord record) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_undo_records.push_back(std::move(record));
}

void Transaction::clear_undo_records() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_undo_records.clear();
}

void Transaction::add_lock(concurrency::LockResource resource) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_locks.push_back(resource);
}

void Transaction::clear_locks() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_locks.clear();
}

} // namespace nemo::transaction
