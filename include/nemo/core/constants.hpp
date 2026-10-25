#ifndef NEMO_CORE_CONSTANTS_HPP
#define NEMO_CORE_CONSTANTS_HPP

#include <cstddef>

namespace nemo::core {

// Default size of a database page. Will be part of the storage-engine design in Module 2.
constexpr std::size_t DEFAULT_PAGE_SIZE = 8192;

// Default size for the buffer pool memory.
constexpr std::size_t DEFAULT_BUFFER_POOL_SIZE = 64 * 1024 * 1024; // 64 MB

} // namespace nemo::core

#endif // NEMO_CORE_CONSTANTS_HPP
