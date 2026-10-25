#include "nemo/index/bplus_tree_node.hpp"
#include "nemo/storage/page_header.hpp"
#include <cstring>
#include <algorithm>

namespace nemo::index {

BPlusTreeNode::BPlusTreeNode(storage::PageHandle handle) 
    : m_handle(std::move(handle)) {}

BPlusTreePageHeader* BPlusTreeNode::header() {
    return reinterpret_cast<BPlusTreePageHeader*>(
        m_handle.get_mut().payload_span().data() - sizeof(storage::PageHeader) + sizeof(storage::PageHeader)
    );
}

const BPlusTreePageHeader* BPlusTreeNode::header() const {
    return reinterpret_cast<const BPlusTreePageHeader*>(
        m_handle.get().payload_span().data() - sizeof(storage::PageHeader) + sizeof(storage::PageHeader)
    );
}

std::size_t BPlusTreeNode::page_size() const {
    return m_handle.get().payload_span().size() + sizeof(storage::PageHeader);
}

void BPlusTreeNode::set_parent_page_id(PageID parent_id) {
    header()->parent_page_id = parent_id;
    m_handle.mark_dirty();
}

BPlusTreeNode::EntrySlot* BPlusTreeNode::slot(std::uint16_t index) {
    auto* hdr = header();
    std::byte* slot_area = reinterpret_cast<std::byte*>(hdr) + sizeof(BPlusTreePageHeader);
    return reinterpret_cast<EntrySlot*>(slot_area) + index;
}

const BPlusTreeNode::EntrySlot* BPlusTreeNode::slot(std::uint16_t index) const {
    const auto* hdr = header();
    const std::byte* slot_area = reinterpret_cast<const std::byte*>(hdr) + sizeof(BPlusTreePageHeader);
    return reinterpret_cast<const EntrySlot*>(slot_area) + index;
}

std::uint16_t BPlusTreeNode::write_entry(const std::vector<std::byte>& entry_bytes) {
    auto* hdr = header();
    hdr->free_space_upper_bound -= static_cast<std::uint16_t>(entry_bytes.size());
    
    std::byte* page_data = reinterpret_cast<std::byte*>(hdr) - sizeof(storage::PageHeader);
    std::memcpy(page_data + hdr->free_space_upper_bound, entry_bytes.data(), entry_bytes.size());
    
    return hdr->free_space_upper_bound;
}

void BPlusTreeNode::make_slot_room(std::uint16_t index) {
    auto* hdr = header();
    if (index < hdr->key_count) {
        EntrySlot* slots = slot(0);
        // Memmove to the right by 1
        std::memmove(&slots[index + 1], &slots[index], static_cast<std::size_t>(hdr->key_count - index) * sizeof(EntrySlot));
    }
    hdr->key_count++;
    hdr->free_space_lower_bound = static_cast<std::uint16_t>(hdr->free_space_lower_bound + sizeof(EntrySlot));
}

void BPlusTreeNode::erase_slot(std::uint16_t index) {
    auto* hdr = header();
    if (index < hdr->key_count - 1) {
        EntrySlot* slots = slot(0);
        // Memmove to the left by 1
        std::memmove(&slots[index], &slots[index + 1], static_cast<std::size_t>(hdr->key_count - index - 1) * sizeof(EntrySlot));
    }
    hdr->key_count--;
    hdr->free_space_lower_bound = static_cast<std::uint16_t>(hdr->free_space_lower_bound - sizeof(EntrySlot));
}

void BPlusTreeNode::compact() {
    auto* hdr = header();
    
    // Sort slots by offset descending (from end of page to middle)
    struct SlotRef {
        std::uint16_t id;
        std::uint16_t offset;
        std::uint16_t length;
    };
    
    std::vector<SlotRef> live_slots;
    live_slots.reserve(hdr->key_count);
    
    EntrySlot* slots = slot(0);
    for (std::uint16_t i = 0; i < hdr->key_count; ++i) {
        live_slots.push_back({i, slots[i].offset, slots[i].length});
    }
    
    std::sort(live_slots.begin(), live_slots.end(), [](const SlotRef& a, const SlotRef& b) {
        return a.offset > b.offset; // Largest offset first
    });
    
    std::byte* page_data = reinterpret_cast<std::byte*>(hdr) - sizeof(storage::PageHeader);
    std::uint16_t write_cursor = static_cast<std::uint16_t>(page_size());
    std::vector<std::byte> temp_buf;
    
    for (const auto& sr : live_slots) {
        write_cursor = static_cast<std::uint16_t>(write_cursor - sr.length);
        if (sr.offset != write_cursor) {
            temp_buf.assign(page_data + sr.offset, page_data + sr.offset + sr.length);
            std::memcpy(page_data + write_cursor, temp_buf.data(), sr.length);
            slots[sr.id].offset = write_cursor;
        }
    }
    
    hdr->free_space_upper_bound = write_cursor;
    
    // Recalculate true free space
    hdr->free_space = static_cast<std::uint16_t>(hdr->free_space_upper_bound - hdr->free_space_lower_bound);
    m_handle.mark_dirty();
}

} // namespace nemo::index
