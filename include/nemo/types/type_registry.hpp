#ifndef NEMO_TYPES_TYPE_REGISTRY_HPP
#define NEMO_TYPES_TYPE_REGISTRY_HPP

#include "nemo/types/type_info.hpp"
#include <string_view>
#include <optional>

namespace nemo::types {

class TypeRegistry {
public:
    // Case-insensitive lookup of type name or alias.
    // E.g., "INT" -> TypeID::Integer, "CHARACTER VARYING" -> TypeID::VarChar
    static std::optional<TypeID> lookup_type(std::string_view name) noexcept;
    
    // Check if alias is valid
    static bool is_valid_alias(std::string_view name) noexcept;
};

} // namespace nemo::types

#endif // NEMO_TYPES_TYPE_REGISTRY_HPP
