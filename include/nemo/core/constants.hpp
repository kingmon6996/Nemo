#ifndef NEMO_CORE_CONSTANTS_HPP
#define NEMO_CORE_CONSTANTS_HPP

#include <cstddef>
#include <cstdint>

namespace nemo::core {

// Default size of a database page.
constexpr std::size_t DEFAULT_PAGE_SIZE = 8192;

// Default size for the buffer pool memory.
constexpr std::size_t DEFAULT_BUFFER_POOL_SIZE = 64 * 1024 * 1024; // 64 MB

// Buffer pool size clamping bounds (Phase 2)
constexpr std::size_t BUFFER_POOL_MIN_BYTES = 32ULL  * 1024 * 1024;   // 32 MB floor
constexpr std::size_t BUFFER_POOL_MAX_BYTES = 8192ULL * 1024 * 1024;  // 8 GB ceiling

constexpr std::size_t clamp_buffer_pool_bytes(
    std::size_t requested_bytes, std::size_t available_ram_bytes) noexcept {
    const std::size_t target_bytes = requested_bytes == 0
        ? available_ram_bytes / 4
        : requested_bytes;
    return target_bytes < BUFFER_POOL_MIN_BYTES
        ? BUFFER_POOL_MIN_BYTES
        : target_bytes > BUFFER_POOL_MAX_BYTES
            ? BUFFER_POOL_MAX_BYTES
            : target_bytes;
}

} // namespace nemo::core

#endif // NEMO_CORE_CONSTANTS_HPP
