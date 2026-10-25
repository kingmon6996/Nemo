#ifndef NEMO_CORE_TYPES_HPP
#define NEMO_CORE_TYPES_HPP

#include <cstdint>
#include <vector>

namespace nemo::core {

// Basic byte type for raw data
using byte = std::uint8_t;

// Buffer for raw data
using ByteBuffer = std::vector<byte>;

// File offsets and sizes (persistent)
using FileOffset = std::uint64_t;
using FileSize = std::uint64_t;

// Note: PageID, RowID, TableID, IndexID, TransactionID are defined in ids.hpp as strong types.
// The types here are primitives used internally.

using Timestamp = std::uint64_t;
using ColumnID = std::uint32_t;
using LogSequenceNumber = std::uint64_t;

} // namespace nemo::core

#endif // NEMO_CORE_TYPES_HPP
