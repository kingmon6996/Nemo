#ifndef NEMO_CONCURRENCY_LOCK_HANDLE_HPP
#define NEMO_CONCURRENCY_LOCK_HANDLE_HPP

#include "nemo/concurrency/lock_manager.hpp"

namespace nemo::concurrency {

class LockHandle {
public:
    LockHandle() = default;
    LockHandle(LockManager* manager, transaction::TransactionID txn_id, LockResource resource)
        : m_manager(manager), m_txn_id(txn_id), m_resource(resource), m_owns_lock(true) {}

    ~LockHandle() {
        release();
    }
    
    // Move-only semantics
    LockHandle(const LockHandle&) = delete;
    LockHandle& operator=(const LockHandle&) = delete;
    
    LockHandle(LockHandle&& other) noexcept 
        : m_manager(other.m_manager), m_txn_id(other.m_txn_id), 
          m_resource(other.m_resource), m_owns_lock(other.m_owns_lock) {
        other.m_owns_lock = false;
    }
    
    LockHandle& operator=(LockHandle&& other) noexcept {
        if (this != &other) {
            release();
            m_manager = other.m_manager;
            m_txn_id = other.m_txn_id;
            m_resource = other.m_resource;
            m_owns_lock = other.m_owns_lock;
            other.m_owns_lock = false;
        }
        return *this;
    }
    
    void release() {
        if (m_owns_lock && m_manager) {
            (void)m_manager->unlock(m_txn_id, m_resource); // Ignore errors on destructor
            m_owns_lock = false;
        }
    }
    
    // Optional manual release without destruction
    void release_manual() { release(); }

private:
    LockManager* m_manager{nullptr};
    transaction::TransactionID m_txn_id{0};
    LockResource m_resource{};
    bool m_owns_lock{false};
};

} // namespace nemo::concurrency

#endif // NEMO_CONCURRENCY_LOCK_HANDLE_HPP
