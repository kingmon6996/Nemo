#ifndef NEMO_RUNTIME_WORKER_POOL_HPP
#define NEMO_RUNTIME_WORKER_POOL_HPP

#include "nemo/runtime/worker.hpp"
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>

namespace nemo::runtime {

class WorkerPool {
public:
    WorkerPool(std::size_t num_workers, std::size_t max_queue_size, RuntimeMetrics& metrics);
    ~WorkerPool();

    // Start all workers
    void start();
    
    // Shutdown the pool gracefully
    void shutdown();

    // Submit a task. Blocks if the queue is full and blocking is required.
    bool submit(QueryTask task, bool block_if_full = true);
    
    // Try to submit without blocking. Returns false if queue is full.
    bool try_submit(QueryTask task);
    
    std::size_t size() const;
    std::size_t active_workers() const;
    std::size_t queued_tasks() const;
    
    // For workers to pull tasks
    bool wait_for_task(QueryTask& out_task);

private:
    std::size_t m_num_workers;
    std::size_t m_max_queue_size;
    RuntimeMetrics& m_metrics;
    
    std::vector<std::unique_ptr<Worker>> m_workers;
    
    mutable std::mutex m_mutex;
    std::condition_variable m_cv_tasks;
    std::condition_variable m_cv_capacity;
    std::deque<QueryTask> m_tasks;
    std::atomic<std::size_t> m_active_workers_count{0};
    bool m_shutdown{false};
    
    friend class Worker;
    void notify_worker_active() { m_active_workers_count.fetch_add(1, std::memory_order_relaxed); }
    void notify_worker_idle() { m_active_workers_count.fetch_sub(1, std::memory_order_relaxed); }
};

} // namespace nemo::runtime

#endif // NEMO_RUNTIME_WORKER_POOL_HPP
