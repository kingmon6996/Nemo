#ifndef NEMO_CORE_CONFIG_HPP
#define NEMO_CORE_CONFIG_HPP

#include <cstddef>

namespace nemo {

// Database configuration.
class Config {
public:
    Config();

    std::size_t page_size() const noexcept { return m_page_size; }
    std::size_t buffer_pool_size() const noexcept { return m_buffer_pool_size; }
    std::size_t max_connections() const noexcept { return m_max_connections; }

    void set_page_size(std::size_t size) noexcept { m_page_size = size; }
    void set_buffer_pool_size(std::size_t size) noexcept { m_buffer_pool_size = size; }
    void set_max_connections(std::size_t count) noexcept { m_max_connections = count; }

private:
    std::size_t m_page_size;
    std::size_t m_buffer_pool_size;
    std::size_t m_max_connections;
};

} // namespace nemo

#endif // NEMO_CORE_CONFIG_HPP
