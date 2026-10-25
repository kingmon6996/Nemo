#ifndef NEMO_STORAGE_PAGE_HEADER_HPP
#define NEMO_STORAGE_PAGE_HEADER_HPP

#include "nemo/core/ids.hpp"
#include "nemo/core/types.hpp"
#include <cstdint>
#include <span>

namespace nemo::storage {

enum class PageType : std::uint8_t {
    Invalid = 0,
    Metadata,
    Table,
    Index,
    FreeSpace,
    Overflow,
    Internal
};

// Represents the parsed page header.
struct PageHeader {
    PageID page_id;
    PageType page_type;
    std::uint8_t flags;
    std::uint16_t header_version;
    core::LogSequenceNumber lsn;
    std::uint64_t generation;
    std::uint32_t payload_size;
    std::uint32_t checksum;

    // The size of the header when serialized
    static constexpr std::size_t SIZE = 8 + 1 + 1 + 2 + 8 + 8 + 4 + 4; // 36 bytes

    void serialize(std::span<std::byte> buffer) const noexcept;
    void deserialize(std::span<const std::byte> buffer) noexcept;
};

} // namespace nemo::storage

#endif // NEMO_STORAGE_PAGE_HEADER_HPP
