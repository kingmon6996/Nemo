#include "nemo/storage/page_header.hpp"
#include "nemo/storage/serialization.hpp"

namespace nemo::storage {

void PageHeader::serialize(std::span<std::byte> buffer) const noexcept {
    // Layout:
    // 0-7:   page_id (u64)
    // 8:     page_type (u8)
    // 9:     flags (u8)
    // 10-11: header_version (u16)
    // 12-19: lsn (u64)
    // 20-27: generation (u64)
    // 28-31: payload_size (u32)
    // 32-35: checksum (u32)

    write_u64_le(buffer, 0, page_id.value());
    buffer[8] = static_cast<std::byte>(page_type);
    buffer[9] = static_cast<std::byte>(flags);
    write_u16_le(buffer, 10, header_version);
    write_u64_le(buffer, 12, lsn);
    write_u64_le(buffer, 20, generation);
    write_u32_le(buffer, 28, payload_size);
    write_u32_le(buffer, 32, checksum);
}

void PageHeader::deserialize(std::span<const std::byte> buffer) noexcept {
    page_id = PageID(read_u64_le(buffer, 0));
    page_type = static_cast<PageType>(buffer[8]);
    flags = static_cast<std::uint8_t>(buffer[9]);
    header_version = read_u16_le(buffer, 10);
    lsn = read_u64_le(buffer, 12);
    generation = read_u64_le(buffer, 20);
    payload_size = read_u32_le(buffer, 28);
    checksum = read_u32_le(buffer, 32);
}

} // namespace nemo::storage
