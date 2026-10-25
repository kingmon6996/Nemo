#ifndef NEMO_STORAGE_CLOCK_REPLACER_HPP
#define NEMO_STORAGE_CLOCK_REPLACER_HPP

#include "nemo/storage/replacer.hpp"
#include <vector>
#include <mutex>
#include <cstdint>

namespace nemo::storage {

class ClockReplacer : public Replacer {
public:
    explicit ClockReplacer(std::size_t num_frames);
    ~ClockReplacer() override = default;

    std::optional<FrameID> victim() override;
    void pin(FrameID frame_id) override;
    void unpin(FrameID frame_id) override;
    void record_access(FrameID frame_id) override;

private:
    struct FrameState {
        bool is_pinned{false};
        bool reference_bit{false};
        bool is_empty{true};
    };

    std::size_t m_num_frames;
    std::size_t m_clock_hand{0};
    std::vector<FrameState> m_frames;
    std::mutex m_mutex;
};

} // namespace nemo::storage

#endif // NEMO_STORAGE_CLOCK_REPLACER_HPP
