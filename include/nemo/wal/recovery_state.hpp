#ifndef NEMO_WAL_RECOVERY_STATE_HPP
#define NEMO_WAL_RECOVERY_STATE_HPP

namespace nemo::wal {

enum class RecoveryState {
    NotRequired,
    Required,
    Analysis,
    Redo,
    Undo,
    Complete,
    Failed
};

} // namespace nemo::wal

#endif // NEMO_WAL_RECOVERY_STATE_HPP
