#ifndef NEMO_INDEX_BPLUS_TREE_NODE_HPP
#define NEMO_INDEX_BPLUS_TREE_NODE_HPP

#include "nemo/storage/page_handle.hpp"
#include "nemo/index/bplus_tree_page.hpp"
#include "nemo/index/index_key.hpp"
#include <vector>

namespace nemo::index {

// Base class wrapping a PageHandle for B+ Tree nodes.
// We use a slotted architecture within the B+ tree nodes to support variable length keys cleanly.
class BPlusTreeNode {
public:
    explicit BPlusTreeNode(storage::PageHandle handle);
    virtual ~BPlusTreeNode() = default;

    PageID page_id() const { return m_handle.get().id(); }
    BPlusNodeType node_type() const { return header()->node_type; }
    
    bool is_leaf() const { return node_type() == BPlusNodeType::Leaf; }
    bool is_internal() const { return node_type() == BPlusNodeType::Internal; }

    std::uint16_t key_count() const { return header()->key_count; }
    PageID parent_page_id() const { return header()->parent_page_id; }
    void set_parent_page_id(PageID parent_id);

    std::uint16_t free_space() const { return header()->free_space; }

    // Reclaims space if keys have been deleted and fragmented.
    void compact();

    storage::PageHandle handle() && { return std::move(m_handle); }

    BPlusTreePageHeader* header();
    const BPlusTreePageHeader* header() const;
    
    std::size_t page_size() const;

protected:
    storage::PageHandle m_handle;

    
    // Slotted offset struct for entries
    struct EntrySlot {
        std::uint16_t offset;
        std::uint16_t length;
    };
    
    EntrySlot* slot(std::uint16_t index);
    const EntrySlot* slot(std::uint16_t index) const;

    // Writes an entry (key + payload) and returns its offset, decreasing free space.
    std::uint16_t write_entry(const std::vector<std::byte>& entry_bytes);
    
    // Shifts slots left or right to make room for insertion at `index`
    void make_slot_room(std::uint16_t index);
    
    // Shifts slots to cover deletion at `index`
    void erase_slot(std::uint16_t index);
};

} // namespace nemo::index

#endif // NEMO_INDEX_BPLUS_TREE_NODE_HPP
