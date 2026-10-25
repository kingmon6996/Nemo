#include "nemo/index/index_comparator.hpp"
#include <algorithm>

namespace nemo::index {

int IndexComparator::compare(const IndexKey& lhs, const IndexKey& rhs) noexcept {
    std::size_t min_size = std::min(lhs.size(), rhs.size());
    
    for (std::size_t i = 0; i < min_size; ++i) {
        const auto& lval = lhs.at(i);
        const auto& rval = rhs.at(i);
        
        if (lval.is_null() && !rval.is_null()) {
            return -1; // Null is less
        }
        if (!lval.is_null() && rval.is_null()) {
            return 1;
        }
        if (lval.is_null() && rval.is_null()) {
            continue; // Equals, move to next column
        }
        
        if (lval < rval) {
            return -1;
        }
        if (rval < lval) {
            return 1;
        }
    }
    
    // If we're here, all compared columns are equal.
    // The shorter key is considered less (prefix matches).
    if (lhs.size() < rhs.size()) return -1;
    if (lhs.size() > rhs.size()) return 1;
    
    return 0;
}

} // namespace nemo::index
