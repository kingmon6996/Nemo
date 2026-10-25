#include "nemo/storage/database_header.hpp"
#include "nemo/storage/serialization.hpp"
#include <cstring>

namespace nemo::storage {

void DatabaseHeader::serialize(std::span<std::byte> buffer) const noexcept {
    // Layout:
    // 0-7:   magic (8 bytes)
    // 8-11:  format_version (u32)
    // 12-15: page_size (u32)
    // 16-31: database_id (16 bytes)
    // 32-39: creation_timestamp (u64)
    // 40-47: catalog_root_page (u64)
    // 48-55: free_page_root (u64)
    // 56-63: next_page_id (u64)
    // 64-71: flags (u64)
    // 72-75: checksum (u32)

    std::memcpy(buffer.data(), magic.data(), 8);
    write_u32_le(buffer, 8, format_version);
    write_u32_le(buffer, 12, page_size);
    std::memcpy(buffer.data() + 16, database_id.data(), UUID_SIZE);
    write_u64_le(buffer, 32, creation_timestamp);
    write_u64_le(buffer, 40, catalog_root_page.value());
    write_u64_le(buffer, 48, free_page_root.value());
    write_u64_le(buffer, 56, next_page_id.value());
    write_u64_le(buffer, 64, flags);
    write_u32_le(buffer, 72, checksum);
}

void DatabaseHeader::deserialize(std::span<const std::byte> buffer) noexcept {
    std::memcpy(magic.data(), buffer.data(), 8);
    format_version = read_u32_le(buffer, 8);
    page_size = read_u32_le(buffer, 12);
    std::memcpy(database_id.data(), buffer.data() + 16, UUID_SIZE);
    creation_timestamp = read_u64_le(buffer, 32);
    catalog_root_page = PageID(read_u64_le(buffer, 40));
    free_page_root = PageID(read_u64_le(buffer, 48));
    next_page_id = PageID(read_u64_le(buffer, 56));
    flags = read_u64_le(buffer, 64);
    checksum = read_u32_le(buffer, 72);
}

} // namespace nemo::storage
