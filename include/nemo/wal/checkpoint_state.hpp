#ifndef NEMO_WAL_CHECKPOINT_STATE_HPP
#define NEMO_WAL_CHECKPOINT_STATE_HPP

#include <cstdint>
#include <optional>
#include <vector>

namespace nemo::wal {

struct CheckpointTransaction {
    std::uint64_t transaction_id;
    std::uint64_t first_lsn;
    std::uint64_t last_lsn;
};

struct CheckpointDirtyPage {
    std::uint64_t page_id;
    std::uint64_t rec_lsn;
};

struct CheckpointState {
    std::uint64_t max_transaction_id{0};
    std::vector<CheckpointTransaction> transactions;
    std::vector<CheckpointDirtyPage> dirty_pages;
};

std::vector<std::uint8_t> serialize_checkpoint_state(const CheckpointState& state);
std::optional<CheckpointState> deserialize_checkpoint_state(
    const std::vector<std::uint8_t>& payload);

} // namespace nemo::wal

#endif // NEMO_WAL_CHECKPOINT_STATE_HPP
