#ifndef NEMO_RUNTIME_SHUTDOWN_HPP
#define NEMO_RUNTIME_SHUTDOWN_HPP

#include <atomic>

namespace nemo::runtime {

enum class ShutdownState {
    RUNNING,
    QUIESCING,
    SHUTTING_DOWN,
    CLOSED
};

class DatabaseShutdown {
public:
    DatabaseShutdown() = default;
    
    ShutdownState state() const noexcept {
        return m_state.load(std::memory_order_acquire);
    }
    
    void set_state(ShutdownState new_state) noexcept {
        m_state.store(new_state, std::memory_order_release);
    }
    
    bool is_running() const noexcept {
        return state() == ShutdownState::RUNNING;
    }

private:
    std::atomic<ShutdownState> m_state{ShutdownState::RUNNING};
};

} // namespace nemo::runtime

#endif // NEMO_RUNTIME_SHUTDOWN_HPP
