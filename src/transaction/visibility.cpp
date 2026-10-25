#include "nemo/transaction/visibility.hpp"

namespace nemo::transaction {

VisibilityManager::VisibilityManager(TransactionRegistry& registry) : m_registry(registry) {}

bool VisibilityManager::is_visible(const MVCCHeader& header, const Snapshot& snapshot, TransactionID current_txn) const {
    // 1. Is the creator committed (or is it our own transaction)?
    bool created_visible = false;
    
    if (header.xmin == current_txn) {
        // We created it.
        created_visible = true;
    } else if (snapshot.is_active(header.xmin)) {
        // It was active when our snapshot was taken. We cannot see it.
        created_visible = false;
    } else {
        // It was not active when the snapshot started.
        // It must either be committed or aborted before our snapshot.
        if (m_registry.is_committed(header.xmin)) {
            created_visible = true;
        } else {
            // It aborted. Invisible.
            created_visible = false;
        }
    }
    
    if (!created_visible) {
        return false; // If the creation isn't visible, the row isn't visible.
    }
    
    // 2. Was it deleted/updated by a transaction that is visible to us?
    if (header.xmax == INVALID_TRANSACTION_ID) {
        // Not deleted. It is visible.
        return true;
    }
    
    bool deleter_visible = false;
    if (header.xmax == current_txn) {
        // We deleted it ourselves. We can see our own deletes.
        deleter_visible = true;
    } else if (snapshot.is_active(header.xmax)) {
        // The deleter was active when our snapshot started. We cannot see the deletion yet.
        deleter_visible = false;
    } else {
        // Deleter finished before our snapshot. Did they commit?
        if (m_registry.is_committed(header.xmax)) {
            deleter_visible = true;
        } else {
            deleter_visible = false; // Deleter aborted. Their delete doesn't count.
        }
    }
    
    // If the deleter IS visible, then this version is DEAD to us.
    // Therefore, the row is visible ONLY IF the deleter is NOT visible.
    return !deleter_visible;
}

} // namespace nemo::transaction
