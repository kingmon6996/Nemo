#ifndef NEMO_RUNTIME_RESOURCE_POOL_HPP
#define NEMO_RUNTIME_RESOURCE_POOL_HPP

#include <vector>
#include <mutex>
#include <memory>
#include <functional>

namespace nemo::runtime {

template <typename T>
class ResourcePool {
public:
    using Factory = std::function<std::unique_ptr<T>()>;
    using Resetter = std::function<void(T&)>;

    ResourcePool(std::size_t max_cached, Factory factory, Resetter resetter = [](T&){})
        : m_max_cached(max_cached), m_factory(std::move(factory)), m_resetter(std::move(resetter)) {}

    std::unique_ptr<T> acquire() {
        std::unique_ptr<T> obj;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_pool.empty()) {
                obj = std::move(m_pool.back());
                m_pool.pop_back();
            }
        }
        
        if (!obj) {
            obj = m_factory();
        }
        return obj;
    }

    void release(std::unique_ptr<T> obj) {
        if (!obj) return;
        
        m_resetter(*obj);
        
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_pool.size() < m_max_cached) {
            m_pool.push_back(std::move(obj));
        }
        // Else it goes out of scope and is destroyed
    }
    
    std::size_t cached_size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pool.size();
    }

private:
    std::size_t m_max_cached;
    Factory m_factory;
    Resetter m_resetter;
    
    mutable std::mutex m_mutex;
    std::vector<std::unique_ptr<T>> m_pool;
};

} // namespace nemo::runtime

#endif // NEMO_RUNTIME_RESOURCE_POOL_HPP
