#ifndef NEMO_INDEX_BPLUS_TREE_PAGE_HPP
#define NEMO_INDEX_BPLUS_TREE_PAGE_HPP

#include "nemo/core/ids.hpp"
#include <cstdint>

namespace nemo::index {

enum class BPlusNodeType : std::uint8_t {
    Leaf = 0,
    Internal = 1
};

// The header placed at the beginning of a B+ Tree page (after the generic PageHeader)
struct alignas(8) BPlusTreePageHeader {
    BPlusNodeType node_type;
    std::uint8_t level; // 0 = leaf, 1 = parent of leaf, etc.
    std::uint16_t key_count;
    
    // Total free space in the page (excluding header and slots).
    // Not strictly needed if we calculate dynamically, but helps avoid recalculating.
    std::uint16_t free_space; 
    
    // Bounds for slotted layout within the page
    std::uint16_t free_space_lower_bound;
    std::uint16_t free_space_upper_bound;

    PageID parent_page_id{};
    
    // For Leaf nodes (padding for internal nodes to keep headers uniform if desired, or union)
    PageID next_page_id{};
    PageID prev_page_id{};
    
    // We pad to 24 bytes
    std::uint32_t padding; 
};

} // namespace nemo::index

#endif // NEMO_INDEX_BPLUS_TREE_PAGE_HPP
