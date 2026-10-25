#ifndef NEMO_STORAGE_REPLACER_HPP
#define NEMO_STORAGE_REPLACER_HPP

#include "nemo/core/ids.hpp"
#include <optional>

namespace nemo::storage {

// Abstract interface for cache replacement policies.
class Replacer {
public:
    virtual ~Replacer() = default;

    // Returns a victim frame ID to be evicted.
    virtual std::optional<FrameID> victim() = 0;

    // Called when a frame is pinned (in use). It cannot be evicted.
    virtual void pin(FrameID frame_id) = 0;

    // Called when a frame is unpinned (no longer in use). It becomes a candidate for eviction.
    virtual void unpin(FrameID frame_id) = 0;

    // Records that a frame was accessed.
    virtual void record_access(FrameID frame_id) = 0;
};

} // namespace nemo::storage

#endif // NEMO_STORAGE_REPLACER_HPP
