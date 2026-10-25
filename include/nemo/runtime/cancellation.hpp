#ifndef NEMO_RUNTIME_CANCELLATION_HPP
#define NEMO_RUNTIME_CANCELLATION_HPP

#include <atomic>
#include <chrono>
#include <optional>

namespace nemo::runtime {

class CancellationToken {
public:
    CancellationToken() = default;
    explicit CancellationToken(std::chrono::steady_clock::time_point deadline) 
        : m_deadline(deadline) {}

    void cancel() noexcept {
        m_cancelled.store(true, std::memory_order_relaxed);
    }

    bool is_cancelled() const noexcept {
        if (m_cancelled.load(std::memory_order_relaxed)) {
            return true;
        }
        if (m_deadline.has_value() && std::chrono::steady_clock::now() > *m_deadline) {
            return true;
        }
        return false;
    }

    void set_deadline(std::chrono::steady_clock::time_point deadline) noexcept {
        m_deadline = deadline;
    }
    
    void reset() noexcept {
        m_cancelled.store(false, std::memory_order_relaxed);
        m_deadline = std::nullopt;
    }

private:
    std::atomic<bool> m_cancelled{false};
    std::optional<std::chrono::steady_clock::time_point> m_deadline;
};

} // namespace nemo::runtime

#endif // NEMO_RUNTIME_CANCELLATION_HPP
