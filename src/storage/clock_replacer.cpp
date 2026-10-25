#include "nemo/storage/clock_replacer.hpp"

namespace nemo::storage {

ClockReplacer::ClockReplacer(std::size_t num_frames)
    : m_num_frames(num_frames), m_frames(num_frames) {
}

std::optional<FrameID> ClockReplacer::victim() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // If there are no frames, return early
    if (m_num_frames == 0) {
        return std::nullopt;
    }

    // We do at most two full loops around the clock.
    // In the first loop, we might clear reference bits.
    // In the worst case, everything is pinned, and we do one loop doing nothing.
    for (std::size_t i = 0; i < m_num_frames * 2; ++i) {
        auto& state = m_frames[m_clock_hand];
        
        // If the frame is currently pinned or empty, it cannot be a victim.
        if (state.is_pinned || state.is_empty) {
            m_clock_hand = (m_clock_hand + 1) % m_num_frames;
            continue;
        }

        // If it's unpinned and has a reference bit, give it a second chance.
        if (state.reference_bit) {
            state.reference_bit = false;
            m_clock_hand = (m_clock_hand + 1) % m_num_frames;
            continue;
        }

        // Found a victim (unpinned, reference_bit == false).
        // It will be pinned by the caller after eviction, so we mark it empty for now
        // to prevent it from being chosen again until it is re-populated/unpinned.
        state.is_empty = true;
        FrameID victim_id(static_cast<std::uint32_t>(m_clock_hand));
        
        m_clock_hand = (m_clock_hand + 1) % m_num_frames;
        return victim_id;
    }

    // All frames are pinned.
    return std::nullopt;
}

void ClockReplacer::pin(FrameID frame_id) {
    if (frame_id.value() >= m_num_frames) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_frames[frame_id.value()].is_pinned = true;
    m_frames[frame_id.value()].is_empty = false;
}

void ClockReplacer::unpin(FrameID frame_id) {
    if (frame_id.value() >= m_num_frames) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_frames[frame_id.value()].is_pinned = false;
    m_frames[frame_id.value()].is_empty = false;
    // Note: We do NOT clear the reference bit here. 
    // The reference bit is set via record_access and cleared by the victim() search.
}

void ClockReplacer::record_access(FrameID frame_id) {
    if (frame_id.value() >= m_num_frames) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_frames[frame_id.value()].reference_bit = true;
}

} // namespace nemo::storage
