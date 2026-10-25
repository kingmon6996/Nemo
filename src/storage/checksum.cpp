#include "nemo/storage/checksum.hpp"

namespace nemo::storage {

namespace {

// CRC32C (Castagnoli) lookup table.
// Computed dynamically on first use for simplicity, or we could hardcode it.
// To keep things simple and dependency-free, we'll hardcode the table or compute it once.
// Let's compute it once thread-safely via a static initialization.

constexpr std::uint32_t CRC32C_POLYNOMIAL = 0x82f63b78;

struct Crc32cTable {
    std::uint32_t table[256];
    
    constexpr Crc32cTable() : table{} {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int j = 0; j < 8; ++j) {
                if (c & 1) {
                    c = CRC32C_POLYNOMIAL ^ (c >> 1);
                } else {
                    c >>= 1;
                }
            }
            table[i] = c;
        }
    }
};

constexpr Crc32cTable CRC_TABLE;

} // namespace

std::uint32_t calculate_checksum(std::span<const std::byte> data) noexcept {
    std::uint32_t crc = 0xFFFFFFFF;
    for (std::byte b : data) {
        std::uint8_t index = static_cast<std::uint8_t>(crc ^ static_cast<std::uint32_t>(b));
        crc = CRC_TABLE.table[index] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

} // namespace nemo::storage
