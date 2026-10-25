#ifndef NEMO_STORAGE_SERIALIZATION_HPP
#define NEMO_STORAGE_SERIALIZATION_HPP

#include <cstdint>
#include <cstddef>
#include <span>

namespace nemo::storage {

// Explicit little-endian serialization functions.
// These functions use bit shifting to be 100% architecture independent.

inline void write_u16_le(std::span<std::byte> buffer, std::size_t offset, std::uint16_t value) noexcept {
    buffer[offset]     = static_cast<std::byte>(value & 0xFF);
    buffer[offset + 1] = static_cast<std::byte>((value >> 8) & 0xFF);
}

inline void write_u32_le(std::span<std::byte> buffer, std::size_t offset, std::uint32_t value) noexcept {
    buffer[offset]     = static_cast<std::byte>(value & 0xFF);
    buffer[offset + 1] = static_cast<std::byte>((value >> 8) & 0xFF);
    buffer[offset + 2] = static_cast<std::byte>((value >> 16) & 0xFF);
    buffer[offset + 3] = static_cast<std::byte>((value >> 24) & 0xFF);
}

inline void write_u64_le(std::span<std::byte> buffer, std::size_t offset, std::uint64_t value) noexcept {
    buffer[offset]     = static_cast<std::byte>(value & 0xFF);
    buffer[offset + 1] = static_cast<std::byte>((value >> 8) & 0xFF);
    buffer[offset + 2] = static_cast<std::byte>((value >> 16) & 0xFF);
    buffer[offset + 3] = static_cast<std::byte>((value >> 24) & 0xFF);
    buffer[offset + 4] = static_cast<std::byte>((value >> 32) & 0xFF);
    buffer[offset + 5] = static_cast<std::byte>((value >> 40) & 0xFF);
    buffer[offset + 6] = static_cast<std::byte>((value >> 48) & 0xFF);
    buffer[offset + 7] = static_cast<std::byte>((value >> 56) & 0xFF);
}

inline std::uint16_t read_u16_le(std::span<const std::byte> buffer, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(buffer[offset]) |
        (static_cast<std::uint16_t>(buffer[offset + 1]) << 8)
    );
}

inline std::uint32_t read_u32_le(std::span<const std::byte> buffer, std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(buffer[offset]) |
           (static_cast<std::uint32_t>(buffer[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(buffer[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(buffer[offset + 3]) << 24);
}

inline std::uint64_t read_u64_le(std::span<const std::byte> buffer, std::size_t offset) noexcept {
    return static_cast<std::uint64_t>(buffer[offset]) |
           (static_cast<std::uint64_t>(buffer[offset + 1]) << 8) |
           (static_cast<std::uint64_t>(buffer[offset + 2]) << 16) |
           (static_cast<std::uint64_t>(buffer[offset + 3]) << 24) |
           (static_cast<std::uint64_t>(buffer[offset + 4]) << 32) |
           (static_cast<std::uint64_t>(buffer[offset + 5]) << 40) |
           (static_cast<std::uint64_t>(buffer[offset + 6]) << 48) |
           (static_cast<std::uint64_t>(buffer[offset + 7]) << 56);
}

} // namespace nemo::storage

#endif // NEMO_STORAGE_SERIALIZATION_HPP
