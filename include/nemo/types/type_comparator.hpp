#ifndef NEMO_TYPES_TYPE_COMPARATOR_HPP
#define NEMO_TYPES_TYPE_COMPARATOR_HPP

#include "nemo/types/value.hpp"

namespace nemo::types {

class TypeComparator {
public:
    // Core comparison primitives.
    // Note: These compare actual values. If one is NULL, behavior depends on context.
    // Here we provide the strict C++ evaluation (NULLs are equal, NULLs sort first).
    // The SQL three-valued logic (NULL == NULL is UNKNOWN) will be built on top of this in the Expression engine.
    
    static bool equal(const Value& lhs, const Value& rhs) noexcept;
    static bool not_equal(const Value& lhs, const Value& rhs) noexcept { return !equal(lhs, rhs); }
    static bool less(const Value& lhs, const Value& rhs) noexcept;
    static bool less_or_equal(const Value& lhs, const Value& rhs) noexcept { return less(lhs, rhs) || equal(lhs, rhs); }
    static bool greater(const Value& lhs, const Value& rhs) noexcept { return !less_or_equal(lhs, rhs); }
    static bool greater_or_equal(const Value& lhs, const Value& rhs) noexcept { return !less(lhs, rhs); }
};

} // namespace nemo::types

#endif // NEMO_TYPES_TYPE_COMPARATOR_HPP
