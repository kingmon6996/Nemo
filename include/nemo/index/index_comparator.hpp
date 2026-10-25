#ifndef NEMO_INDEX_INDEX_COMPARATOR_HPP
#define NEMO_INDEX_INDEX_COMPARATOR_HPP

#include "nemo/index/index_key.hpp"

namespace nemo::index {

class IndexComparator {
public:
    // Compares two index keys lexicographically.
    // Returns:
    //  < 0 if lhs < rhs
    // == 0 if lhs == rhs
    //  > 0 if lhs > rhs
    //
    // Nulls are treated as strictly smaller than non-nulls (NULLS FIRST).
    static int compare(const IndexKey& lhs, const IndexKey& rhs) noexcept;
};

} // namespace nemo::index

#endif // NEMO_INDEX_INDEX_COMPARATOR_HPP
