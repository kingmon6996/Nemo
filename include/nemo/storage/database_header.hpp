#ifndef NEMO_STORAGE_DATABASE_HEADER_HPP
#define NEMO_STORAGE_DATABASE_HEADER_HPP

#include "nemo/core/ids.hpp"
#include "nemo/storage/storage_constants.hpp"
#include <cstdint>
#include <array>
#include <span>

namespace nemo::storage {

// Represents the parsed database header stored in Page 0.
struct DatabaseHeader {
    std::array<char, 8> magic;
    std::uint32_t format_version;
    std::uint32_t page_size;
    std::array<std::byte, UUID_SIZE> database_id;
    std::uint64_t creation_timestamp;
    PageID catalog_root_page;
    PageID metadata_root_page;
    PageID free_page_root;
    PageID next_page_id;
    std::uint64_t flags;
    std::uint32_t checksum;
    // Phase 1: WAL checkpoint LSN — the LSN of the last WAL record
    // flushed to the main data file. Stored at bytes 76-83.
    std::uint64_t checkpoint_lsn{0};

    // The size of the header when serialized (128 bytes total; 36 bytes reserved).
    static constexpr std::size_t SIZE = 128;

    void serialize(std::span<std::byte> buffer) const noexcept;
    void deserialize(std::span<const std::byte> buffer) noexcept;
};

} // namespace nemo::storage

#endif // NEMO_STORAGE_DATABASE_HEADER_HPP
