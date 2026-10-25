#include "nemo/storage/page.hpp"
#include <cassert>

namespace nemo::storage {

Page::Page(PageID id) noexcept
    : m_id(id)
    , m_data(core::DEFAULT_PAGE_SIZE)
    , m_is_dirty(false)
{
}

Page::Page(PageID id, std::size_t page_size)
    : m_id(id)
    , m_data(page_size)
    , m_is_dirty(false)
{
}

std::span<std::byte> Page::header_span() noexcept {
    return std::span<std::byte>(m_data).first(PageHeader::SIZE);
}

std::span<const std::byte> Page::header_span() const noexcept {
    return std::span<const std::byte>(m_data).first(PageHeader::SIZE);
}

std::span<std::byte> Page::payload_span() noexcept {
    return std::span<std::byte>(m_data).subspan(PageHeader::SIZE);
}

std::span<const std::byte> Page::payload_span() const noexcept {
    return std::span<const std::byte>(m_data).subspan(PageHeader::SIZE);
}

PageHeader Page::read_header() const noexcept {
    PageHeader header;
    header.deserialize(header_span());
    return header;
}

void Page::write_header(const PageHeader& header) noexcept {
    header.serialize(header_span());
    mark_dirty();
}

} // namespace nemo::storage
