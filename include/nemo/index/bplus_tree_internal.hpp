#ifndef NEMO_INDEX_BPLUS_TREE_INTERNAL_HPP
#define NEMO_INDEX_BPLUS_TREE_INTERNAL_HPP

#include "nemo/index/bplus_tree_node.hpp"
#include "nemo/index/index_entry.hpp"
#include "nemo/types/type_id.hpp"

namespace nemo::index {

class BPlusTreeInternal : public BPlusTreeNode {
public:
    explicit BPlusTreeInternal(storage::PageHandle handle);

    void init(std::uint8_t level);

    // For internal nodes, index 0 is typically just a PageID (implicit infinitely small key).
    // Keys start at index 1.
    // find_child returns the PageID of the child that should contain the key
    PageID find_child(const IndexKey& key) const;

    // Inserts a new separator key and its right-hand child
    bool insert(const IndexKey& key, PageID right_child);
    
    // Removes a child and its corresponding separator
    bool remove(const IndexKey& key);

    InternalEntry entry_at(std::uint16_t index, const std::vector<types::TypeID>& types) const;
    
    std::span<const std::byte> key_bytes_at(std::uint16_t index) const;
    PageID child_at(std::uint16_t index) const;
    
    void set_child_at(std::uint16_t index, PageID child_id);

    // Moves half of entries to new internal node. Returns the separator key that goes up to parent.
    IndexKey split(BPlusTreeInternal& new_internal, const std::vector<types::TypeID>& types);
};

} // namespace nemo::index

#endif // NEMO_INDEX_BPLUS_TREE_INTERNAL_HPP
