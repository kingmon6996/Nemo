#include "nemo/runtime/worker_pool.hpp"

namespace nemo::runtime {

WorkerPool::WorkerPool(std::size_t num_workers, std::size_t max_queue_size, RuntimeMetrics& metrics)
    : m_num_workers(num_workers), m_max_queue_size(max_queue_size), m_metrics(metrics) {
    
    m_workers.reserve(num_workers);
    for (std::size_t i = 0; i < num_workers; ++i) {
        m_workers.push_back(std::make_unique<Worker>(static_cast<std::uint32_t>(i), this, m_metrics));
    }
}

WorkerPool::~WorkerPool() {
    shutdown();
}

void WorkerPool::start() {
    for (auto& worker : m_workers) {
        worker->start();
    }
}

void WorkerPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_shutdown) return;
        m_shutdown = true;
    }
    m_cv_tasks.notify_all();
    m_cv_capacity.notify_all();
    
    for (auto& worker : m_workers) {
        worker->stop();
    }
}

bool WorkerPool::submit(QueryTask task, bool block_if_full) {
    std::unique_lock<std::mutex> lock(m_mutex);
    
    if (m_shutdown) return false;
    
    if (m_tasks.size() >= m_max_queue_size) {
        if (!block_if_full) {
            return false;
        }
        m_cv_capacity.wait(lock, [this] { return m_shutdown || m_tasks.size() < m_max_queue_size; });
        if (m_shutdown) return false;
    }
    
    m_tasks.push_back(std::move(task));
    m_metrics.task_queue_size.store(m_tasks.size(), std::memory_order_relaxed);
    
    lock.unlock();
    m_cv_tasks.notify_one();
    return true;
}

bool WorkerPool::try_submit(QueryTask task) {
    return submit(std::move(task), false);
}

std::size_t WorkerPool::size() const {
    return m_num_workers;
}

std::size_t WorkerPool::active_workers() const {
    return m_active_workers_count.load(std::memory_order_relaxed);
}

std::size_t WorkerPool::queued_tasks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tasks.size();
}

bool WorkerPool::wait_for_task(QueryTask& out_task) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv_tasks.wait(lock, [this] { return m_shutdown || !m_tasks.empty(); });
    
    if (m_shutdown && m_tasks.empty()) {
        return false;
    }
    
    out_task = std::move(m_tasks.front());
    m_tasks.pop_front();
    m_metrics.task_queue_size.store(m_tasks.size(), std::memory_order_relaxed);
    
    lock.unlock();
    m_cv_capacity.notify_one();
    return true;
}

} // namespace nemo::runtime
