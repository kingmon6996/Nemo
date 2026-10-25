#ifndef NEMO_RUNTIME_METRICS_HPP
#define NEMO_RUNTIME_METRICS_HPP

#include <atomic>
#include <cstdint>

namespace nemo::runtime {

struct RuntimeMetrics {
    std::atomic<std::uint64_t> total_queries{0};
    std::atomic<std::uint64_t> successful_queries{0};
    std::atomic<std::uint64_t> failed_queries{0};
    std::atomic<std::uint64_t> cancelled_queries{0};
    
    std::atomic<std::uint64_t> active_connections{0};
    std::atomic<std::uint64_t> connection_pool_misses{0};
    
    std::atomic<std::uint64_t> active_workers{0};
    std::atomic<std::uint64_t> task_queue_size{0};
    
    // Module 10 integration points
    std::atomic<std::uint64_t> lock_waits{0};
    std::atomic<std::uint64_t> deadlocks{0};
    
    // Module 3/11 integration points can be accessed directly from those managers
};

} // namespace nemo::runtime

#endif // NEMO_RUNTIME_METRICS_HPP
