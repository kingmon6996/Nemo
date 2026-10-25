#ifndef NEMO_STORAGE_PAGE_HPP
#define NEMO_STORAGE_PAGE_HPP

#include "nemo/core/ids.hpp"
#include "nemo/storage/page_header.hpp"
#include "nemo/core/constants.hpp"
#include <vector>
#include <span>

namespace nemo::storage {

// Encapsulates a fixed-size database page in memory.
class Page {
public:
    // Creates a page with default size
    explicit Page(PageID id = PageID()) noexcept;
    
    // Creates a page with a specific size
    Page(PageID id, std::size_t page_size);

    // Disable copy for safety/performance, allow move
    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;
    Page(Page&&) noexcept = default;
    Page& operator=(Page&&) noexcept = default;

    PageID id() const noexcept { return m_id; }
    void set_id(PageID id) noexcept { m_id = id; }

    std::span<std::byte> data() noexcept { return m_data; }
    std::span<const std::byte> data() const noexcept { return m_data; }

    // Convenience spans for header and payload
    std::span<std::byte> header_span() noexcept;
    std::span<const std::byte> header_span() const noexcept;
    
    std::span<std::byte> payload_span() noexcept;
    std::span<const std::byte> payload_span() const noexcept;

    bool is_dirty() const noexcept { return m_is_dirty; }
    void mark_dirty() noexcept { m_is_dirty = true; }
    void clear_dirty() noexcept { m_is_dirty = false; }

    // Parses the header from the page data buffer
    PageHeader read_header() const noexcept;
    
    // Writes the header to the page data buffer
    void write_header(const PageHeader& header) noexcept;

private:
    PageID m_id;
    std::vector<std::byte> m_data;
    bool m_is_dirty{false};
};

} // namespace nemo::storage

#endif // NEMO_STORAGE_PAGE_HPP
