#include "nemo/runtime/worker.hpp"
#include "nemo/runtime/worker_pool.hpp"
#include <iostream>

namespace nemo::runtime {

Worker::Worker(std::uint32_t id, WorkerPool* pool, RuntimeMetrics& metrics)
    : m_id(id), m_pool(pool), m_metrics(metrics) {
    // 256 MB default query limit for the local memory resource.
    m_query_memory = std::make_unique<QueryMemoryResource>(256 * 1024 * 1024);
}

Worker::~Worker() {
    stop();
}

void Worker::start() {
    if (!m_running.exchange(true)) {
        m_thread = std::make_unique<std::thread>(&Worker::loop, this);
    }
}

void Worker::stop() {
    if (m_running.exchange(false)) {
        if (m_thread && m_thread->joinable()) {
            m_thread->join();
        }
    }
}

void Worker::loop() {
    while (m_running.load()) {
        QueryTask task(QueryID{0}, nullptr, nullptr);
        
        if (!m_pool->wait_for_task(task)) {
            break;
        }
        
        m_pool->notify_worker_active();
        
        try {
            // Check cancellation before execution
            if (task.connection() && task.connection()->cancellation_token().is_cancelled()) {
                m_metrics.cancelled_queries.fetch_add(1, std::memory_order_relaxed);
            } else {
                task.execute();
                m_metrics.successful_queries.fetch_add(1, std::memory_order_relaxed);
            }
        } catch (const std::exception& e) {
            // Log error
            m_metrics.failed_queries.fetch_add(1, std::memory_order_relaxed);
            // In a real implementation we would also set the error on the connection/session
            std::cerr << "Worker " << m_id << " caught exception: " << e.what() << std::endl;
        } catch (...) {
            m_metrics.failed_queries.fetch_add(1, std::memory_order_relaxed);
            std::cerr << "Worker " << m_id << " caught unknown exception." << std::endl;
        }
        
        // Reset local temporary resources
        m_query_memory->reset_usage();
        
        m_metrics.total_queries.fetch_add(1, std::memory_order_relaxed);
        m_pool->notify_worker_idle();
    }
}

} // namespace nemo::runtime
