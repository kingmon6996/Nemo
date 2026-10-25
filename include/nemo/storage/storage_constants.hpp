#ifndef NEMO_STORAGE_CONSTANTS_HPP
#define NEMO_STORAGE_CONSTANTS_HPP

#include <cstdint>
#include <array>
#include <cstddef>

namespace nemo::storage {

// The magic string identifying a Nemo database file: "NEMODB\0\0"
constexpr std::array<char, 8> MAGIC_BYTES = {'N', 'E', 'M', 'O', 'D', 'B', '\0', '\0'};

// Database UUID size
constexpr std::size_t UUID_SIZE = 16;

} // namespace nemo::storage

#endif // NEMO_STORAGE_CONSTANTS_HPP
