#ifndef NEMO_TYPES_TYPE_CAST_HPP
#define NEMO_TYPES_TYPE_CAST_HPP

#include "nemo/types/value.hpp"
#include <optional>

namespace nemo::types {

class TypeCast {
public:
    // Casts a value to a target type.
    // Returns nullopt if the cast is invalid or overflows.
    static std::optional<Value> cast(const Value& value, TypeID target_type);
};

} // namespace nemo::types

#endif // NEMO_TYPES_TYPE_CAST_HPP
