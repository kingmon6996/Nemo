#include "nemo/index/bplus_tree_internal.hpp"
#include "nemo/storage/page_header.hpp"
#include <cstring>

namespace nemo::index {

BPlusTreeInternal::BPlusTreeInternal(storage::PageHandle handle) 
    : BPlusTreeNode(std::move(handle)) {}

void BPlusTreeInternal::init(std::uint8_t level) {
    auto* hdr = header();
    hdr->node_type = BPlusNodeType::Internal;
    hdr->level = level;
    hdr->key_count = 0;
    hdr->free_space_lower_bound = static_cast<std::uint16_t>(sizeof(storage::PageHeader) + sizeof(BPlusTreePageHeader));
    hdr->free_space_upper_bound = static_cast<std::uint16_t>(page_size());
    hdr->free_space = static_cast<std::uint16_t>(hdr->free_space_upper_bound - hdr->free_space_lower_bound);
    hdr->parent_page_id = PageID{};
    m_handle.mark_dirty();
}

PageID BPlusTreeInternal::find_child(const IndexKey& key) const {
    auto search_bytes = key.serialize();
    
    // Binary search could be used, linear for simplicity of byte arrays
    // Index 0 has no key, just a child pointer.
    for (std::uint16_t i = 1; i < key_count(); ++i) {
        auto kb = key_bytes_at(i);
        
        int cmp = 0;
        std::size_t min_len = std::min(kb.size(), search_bytes.size());
        for (std::size_t j = 0; j < min_len; ++j) {
            if (kb[j] < search_bytes[j]) { cmp = -1; break; }
            if (kb[j] > search_bytes[j]) { cmp = 1; break; }
        }
        if (cmp == 0) {
            if (kb.size() < search_bytes.size()) cmp = -1;
            else if (kb.size() > search_bytes.size()) cmp = 1;
        }
        
        if (cmp > 0) { // search_bytes < key
            return child_at(i - 1);
        }
    }
    
    return child_at(key_count() - 1);
}

std::span<const std::byte> BPlusTreeInternal::key_bytes_at(std::uint16_t index) const {
    if (index == 0) return {}; // Index 0 has no key
    
    const auto* s = slot(index);
    const std::byte* page_data = reinterpret_cast<const std::byte*>(header()) - sizeof(storage::PageHeader);
    return std::span<const std::byte>(page_data + s->offset, s->length - 4);
}

PageID BPlusTreeInternal::child_at(std::uint16_t index) const {
    const auto* s = slot(index);
    const std::byte* page_data = reinterpret_cast<const std::byte*>(header()) - sizeof(storage::PageHeader);
    
    std::uint32_t p;
    std::memcpy(&p, page_data + s->offset + s->length - 4, 4);
    
    return PageID(p);
}

void BPlusTreeInternal::set_child_at(std::uint16_t index, PageID child_id) {
    const auto* s = slot(index);
    std::byte* page_data = reinterpret_cast<std::byte*>(header()) - sizeof(storage::PageHeader);
    
    std::uint32_t p = static_cast<std::uint32_t>(child_id.value());
    std::memcpy(page_data + s->offset + s->length - 4, &p, 4);
    m_handle.mark_dirty();
}

bool BPlusTreeInternal::insert(const IndexKey& key, PageID right_child) {
    auto key_bytes = key.serialize();
    std::size_t entry_size = key_bytes.size() + 4;
    
    if (entry_size + sizeof(EntrySlot) > free_space()) {
        compact();
        if (entry_size + sizeof(EntrySlot) > free_space()) {
            return false;
        }
    }
    
    // Find index to insert
    std::uint16_t index = 1;
    for (; index < key_count(); ++index) {
        auto kb = key_bytes_at(index);
        
        int cmp = 0;
        std::size_t min_len = std::min(kb.size(), key_bytes.size());
        for (std::size_t j = 0; j < min_len; ++j) {
            if (kb[j] < key_bytes[j]) { cmp = -1; break; }
            if (kb[j] > key_bytes[j]) { cmp = 1; break; }
        }
        if (cmp == 0) {
            if (kb.size() < key_bytes.size()) cmp = -1;
            else if (kb.size() > key_bytes.size()) cmp = 1;
        }
        
        if (cmp >= 0) break; // found where it should go
    }
    
    std::vector<std::byte> entry_buf(entry_size);
    std::memcpy(entry_buf.data(), key_bytes.data(), key_bytes.size());
    std::uint32_t p = static_cast<std::uint32_t>(right_child.value());
    std::memcpy(entry_buf.data() + key_bytes.size(), &p, 4);
    
    make_slot_room(index);
    
    std::uint16_t offset = write_entry(entry_buf);
    
    auto* s_ptr = slot(index);
    s_ptr->offset = offset;
    s_ptr->length = static_cast<std::uint16_t>(entry_size);
    
    header()->free_space = static_cast<std::uint16_t>(header()->free_space_upper_bound - header()->free_space_lower_bound);
    m_handle.mark_dirty();
    
    return true;
}

bool BPlusTreeInternal::remove(const IndexKey& key) {
    auto key_bytes = key.serialize();
    
    for (std::uint16_t index = 1; index < key_count(); ++index) {
        auto kb = key_bytes_at(index);
        if (kb.size() == key_bytes.size() && std::memcmp(kb.data(), key_bytes.data(), kb.size()) == 0) {
            erase_slot(index);
            header()->free_space = static_cast<std::uint16_t>(header()->free_space_upper_bound - header()->free_space_lower_bound);
            m_handle.mark_dirty();
            return true;
        }
    }
    return false;
}

IndexKey BPlusTreeInternal::split(BPlusTreeInternal& new_internal, const std::vector<types::TypeID>& types) {
    std::uint16_t mid = static_cast<std::uint16_t>(key_count() / 2);
    
    // The separator key at mid is pushed UP to the parent, so it does not stay in the internal node.
    IndexKey separator = IndexKey::deserialize(key_bytes_at(mid), types);
    
    // Index 0 of new_internal gets the child of mid
    // We insert a dummy key (empty) for index 0
    std::vector<std::byte> dummy(4);
    std::uint32_t p = static_cast<std::uint32_t>(child_at(mid).value());
    std::memcpy(dummy.data(), &p, 4);
    
    new_internal.make_slot_room(0);
    new_internal.slot(0)->offset = new_internal.write_entry(dummy);
    new_internal.slot(0)->length = 4;
    
    for (std::uint16_t i = mid + 1; i < key_count(); ++i) {
        auto kb = key_bytes_at(i);
        auto child = child_at(i);
        IndexKey k = IndexKey::deserialize(kb, types);
        new_internal.insert(k, child);
    }
    
    // Remove from this node
    std::uint16_t moved = static_cast<std::uint16_t>(key_count() - mid);
    header()->key_count = mid;
    header()->free_space_lower_bound = static_cast<std::uint16_t>(header()->free_space_lower_bound - (moved * sizeof(EntrySlot)));
    compact();
    
    return separator;
}

} // namespace nemo::index
