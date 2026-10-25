#ifndef NEMO_STORAGE_CHECKSUM_HPP
#define NEMO_STORAGE_CHECKSUM_HPP

#include <cstddef>
#include <cstdint>
#include <span>

namespace nemo::storage {

// Deterministic CRC32C checksum function.
// Used for checksumming pages and database headers.
std::uint32_t calculate_checksum(std::span<const std::byte> data) noexcept;

} // namespace nemo::storage

#endif // NEMO_STORAGE_CHECKSUM_HPP
