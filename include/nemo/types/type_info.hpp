#ifndef NEMO_TYPES_TYPE_INFO_HPP
#define NEMO_TYPES_TYPE_INFO_HPP

#include "nemo/types/type_id.hpp"
#include <string_view>

namespace nemo::types {

struct TypeInfo {
    TypeID id;
    std::string_view canonical_name;
    
    std::size_t fixed_size;
    bool is_variable_length;
    
    bool is_orderable;
    bool is_hashable;
    bool is_indexable;
    bool is_numeric;
    bool is_collection;
    bool is_complex;
};

// Returns static info for a given type ID.
const TypeInfo& get_type_info(TypeID id) noexcept;

} // namespace nemo::types

#endif // NEMO_TYPES_TYPE_INFO_HPP
