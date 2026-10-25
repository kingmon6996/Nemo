#ifndef NEMO_TYPES_TYPE_HASHER_HPP
#define NEMO_TYPES_TYPE_HASHER_HPP

#include "nemo/types/value.hpp"
#include <cstddef>

namespace nemo::types {

class TypeHasher {
public:
    // Computes a deterministic hash for a Value.
    static std::size_t hash(const Value& value) noexcept;
};

} // namespace nemo::types

#endif // NEMO_TYPES_TYPE_HASHER_HPP
