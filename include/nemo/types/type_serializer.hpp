#ifndef NEMO_TYPES_TYPE_SERIALIZER_HPP
#define NEMO_TYPES_TYPE_SERIALIZER_HPP

#include "nemo/types/value.hpp"
#include <vector>
#include <cstddef>

namespace nemo::types {

class TypeSerializer {
public:
    // Serializes a value into a deterministic byte format.
    // Handles endianness explicitely (Little-Endian).
    static std::vector<std::uint8_t> serialize(const Value& value);
    
    // Deserializes a value from bytes, assuming the caller knows the target TypeID.
    static Value deserialize(const std::vector<std::uint8_t>& bytes, TypeID type);
};

} // namespace nemo::types

#endif // NEMO_TYPES_TYPE_SERIALIZER_HPP
