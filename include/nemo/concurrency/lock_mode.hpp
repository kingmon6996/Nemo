#ifndef NEMO_CONCURRENCY_LOCK_MODE_HPP
#define NEMO_CONCURRENCY_LOCK_MODE_HPP

namespace nemo::concurrency {

enum class LockMode {
    Shared,
    Exclusive,
    IntentShared,
    IntentExclusive,
    SharedIntentExclusive
};

// Compatibility Matrix Function
// Returns true if requested lock mode is compatible with the existing lock mode
inline bool is_compatible(LockMode existing, LockMode requested) {
    switch (existing) {
        case LockMode::IntentShared:
            return requested != LockMode::Exclusive;
            
        case LockMode::IntentExclusive:
            return requested == LockMode::IntentShared || requested == LockMode::IntentExclusive;
            
        case LockMode::Shared:
            return requested == LockMode::IntentShared || requested == LockMode::Shared;
            
        case LockMode::SharedIntentExclusive:
            return requested == LockMode::IntentShared;
            
        case LockMode::Exclusive:
            return false; // Exclusive is not compatible with anything
    }
    return false;
}

} // namespace nemo::concurrency

#endif // NEMO_CONCURRENCY_LOCK_MODE_HPP
