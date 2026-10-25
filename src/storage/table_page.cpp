#include "nemo/storage/table_page.hpp"
#include "nemo/storage/slot.hpp"
#include "nemo/storage/page_header.hpp"
#include <cstring>
#include <algorithm>
#include <vector>

namespace nemo::storage {

TablePage::TablePage(PageHandle handle) : m_handle(std::move(handle)) {}

void TablePage::init() {
    auto& page = m_handle.get_mut();
    (void)page;
    
    // Ensure generic page header knows it's a table page (if you add types later)
    // auto* gen_header = reinterpret_cast<PageHeader*>(page.data());
    // gen_header->type = PageType::Table;
    
    auto* tbl_header = header();
    tbl_header->next_page_id = PageID{};
    tbl_header->prev_page_id = PageID{};
    tbl_header->slot_count = 0;
    tbl_header->free_space_lower_bound = sizeof(PageHeader) + sizeof(TablePageHeader);
    tbl_header->free_space_upper_bound = static_cast<std::uint32_t>(page_size());
    tbl_header->live_record_count = 0;
}

std::size_t TablePage::page_size() const {
    return m_handle.get().payload_span().size() + sizeof(PageHeader);
}

TablePageHeader* TablePage::header() {
    return reinterpret_cast<TablePageHeader*>(
        m_handle.get_mut().payload_span().data() - sizeof(PageHeader) + sizeof(PageHeader)
    );
}

const TablePageHeader* TablePage::header() const {
    return reinterpret_cast<const TablePageHeader*>(
        m_handle.get().payload_span().data() - sizeof(PageHeader) + sizeof(PageHeader)
    );
}

PageID TablePage::next_page_id() const { return header()->next_page_id; }
void TablePage::set_next_page_id(PageID id) { header()->next_page_id = id; m_handle.mark_dirty(); }
PageID TablePage::prev_page_id() const { return header()->prev_page_id; }
void TablePage::set_prev_page_id(PageID id) { header()->prev_page_id = id; m_handle.mark_dirty(); }

std::size_t TablePage::free_space() const {
    const auto* hdr = header();
    if (hdr->free_space_upper_bound >= hdr->free_space_lower_bound) {
        return hdr->free_space_upper_bound - hdr->free_space_lower_bound;
    }
    return 0; // Corrupt if upper < lower
}

Result<RecordID> TablePage::insert(const std::vector<std::byte>& record_bytes) {
    if (record_bytes.size() > page_size() - sizeof(PageHeader) - sizeof(TablePageHeader) - sizeof(Slot)) {
        return nemo::Status::InvalidArgument; // RecordTooLarge
    }

    auto* hdr = header();
    
    // Find free slot or add new one
    std::uint32_t target_slot_id = RecordID::INVALID_SLOT;
    auto* slots = reinterpret_cast<Slot*>(
        reinterpret_cast<std::byte*>(hdr) + sizeof(TablePageHeader)
    );
    
    for (std::uint32_t i = 0; i < hdr->slot_count; ++i) {
        if (!slots[i].is_live()) { // deleted or empty
            target_slot_id = i;
            break;
        }
    }

    std::size_t required_space = record_bytes.size();
    if (target_slot_id == RecordID::INVALID_SLOT) {
        required_space += sizeof(Slot);
    }
    
    if (required_space > free_space()) {
        // Not enough space. Could compact, but let's assume we do compaction elsewhere
        // or just return exhausted. We will try compacting first if there is fragmentation.
        // Actually, let's just compact automatically if we suspect fragmentation might free enough space.
        // For simplicity, we just return BufferPoolExhausted. The Heap manages compaction.
        return nemo::Status::InternalError; // BufferPoolExhausted
    }

    if (target_slot_id == RecordID::INVALID_SLOT) {
        target_slot_id = hdr->slot_count;
        hdr->slot_count++;
        hdr->free_space_lower_bound += sizeof(Slot);
    }
    
    // Allocate space from upper bound
    hdr->free_space_upper_bound -= static_cast<std::uint32_t>(record_bytes.size());
    std::uint32_t record_offset = hdr->free_space_upper_bound;
    
    // Write record data
    std::byte* page_data = reinterpret_cast<std::byte*>(hdr) - sizeof(PageHeader);
    std::memcpy(page_data + record_offset, record_bytes.data(), record_bytes.size());
    
    // Update slot
    slots[target_slot_id].offset = record_offset;
    slots[target_slot_id].length = static_cast<std::uint32_t>(record_bytes.size());
    slots[target_slot_id].set_live();
    
    hdr->live_record_count++;
    m_handle.mark_dirty();
    
    return RecordID(page_id(), target_slot_id);
}

Result<std::span<const std::byte>> TablePage::get(const RecordID& id) const {
    if (id.page_id() != page_id()) return nemo::Status::InvalidArgument;
    
    const auto* hdr = header();
    if (id.slot_id() >= hdr->slot_count) return nemo::Status::NotFound;
    
    const auto* slots = reinterpret_cast<const Slot*>(
        reinterpret_cast<const std::byte*>(hdr) + sizeof(TablePageHeader)
    );
    
    const auto& slot = slots[id.slot_id()];
    if (!slot.is_live()) return nemo::Status::NotFound;
    
    const std::byte* page_data = reinterpret_cast<const std::byte*>(hdr) - sizeof(PageHeader);
    
    // Bounds check
    if (slot.offset + slot.length > page_size()) return nemo::Status::InternalError; // Corrupted
    
    return std::span<const std::byte>(page_data + slot.offset, slot.length);
}

Result<void> TablePage::erase(const RecordID& id) {
    if (id.page_id() != page_id()) return nemo::Status::InvalidArgument;
    
    auto* hdr = header();
    if (id.slot_id() >= hdr->slot_count) return nemo::Status::NotFound;
    
    auto* slots = reinterpret_cast<Slot*>(
        reinterpret_cast<std::byte*>(hdr) + sizeof(TablePageHeader)
    );
    
    auto& slot = slots[id.slot_id()];
    if (!slot.is_live()) return nemo::Status::NotFound;
    
    slot.set_deleted();
    hdr->live_record_count--;
    m_handle.mark_dirty();
    
    return {};
}

Result<void> TablePage::update(const RecordID& id, const std::vector<std::byte>& record_bytes) {
    if (id.page_id() != page_id()) return nemo::Status::InvalidArgument;
    
    auto* hdr = header();
    if (id.slot_id() >= hdr->slot_count) return nemo::Status::NotFound;
    
    auto* slots = reinterpret_cast<Slot*>(
        reinterpret_cast<std::byte*>(hdr) + sizeof(TablePageHeader)
    );
    
    auto& slot = slots[id.slot_id()];
    if (!slot.is_live()) return nemo::Status::NotFound;
    
    if (record_bytes.size() <= slot.length) {
        // Fits in-place
        std::byte* page_data = reinterpret_cast<std::byte*>(hdr) - sizeof(PageHeader);
        std::memcpy(page_data + slot.offset, record_bytes.data(), record_bytes.size());
        slot.length = static_cast<std::uint32_t>(record_bytes.size());
        m_handle.mark_dirty();
        return {};
    }
    
    // Needs to move. Do we have free space?
    if (record_bytes.size() > free_space()) {
        return nemo::Status::InternalError; // Need to move to new page (handled by heap)
    }
    
    // Allocate space from upper bound
    hdr->free_space_upper_bound -= static_cast<std::uint32_t>(record_bytes.size());
    std::uint32_t new_offset = hdr->free_space_upper_bound;
    
    std::byte* page_data = reinterpret_cast<std::byte*>(hdr) - sizeof(PageHeader);
    std::memcpy(page_data + new_offset, record_bytes.data(), record_bytes.size());
    
    slot.offset = new_offset;
    slot.length = static_cast<std::uint32_t>(record_bytes.size());
    m_handle.mark_dirty();
    
    return {};
}

Result<void> TablePage::compact() {
    auto* hdr = header();
    auto* slots = reinterpret_cast<Slot*>(
        reinterpret_cast<std::byte*>(hdr) + sizeof(TablePageHeader)
    );
    
    // Create an array of live slots, sorted by offset descending (from right to left)
    struct SlotRef {
        std::uint32_t id;
        std::uint32_t offset;
    };
    
    std::vector<SlotRef> live_slots;
    live_slots.reserve(hdr->live_record_count);
    
    for (std::uint32_t i = 0; i < hdr->slot_count; ++i) {
        if (slots[i].is_live()) {
            live_slots.push_back({i, slots[i].offset});
        }
    }
    
    std::sort(live_slots.begin(), live_slots.end(), [](const SlotRef& a, const SlotRef& b) {
        return a.offset > b.offset; // Largest offset first
    });
    
    std::byte* page_data = reinterpret_cast<std::byte*>(hdr) - sizeof(PageHeader);
    std::uint32_t write_cursor = static_cast<std::uint32_t>(page_size());
    
    // Temporary buffer to hold moving records to avoid overlap issues if moving left
    std::vector<std::byte> temp_buf; 
    
    for (const auto& sr : live_slots) {
        auto& slot = slots[sr.id];
        write_cursor -= slot.length;
        
        if (slot.offset != write_cursor) {
            temp_buf.assign(page_data + slot.offset, page_data + slot.offset + slot.length);
            std::memcpy(page_data + write_cursor, temp_buf.data(), slot.length);
            slot.offset = write_cursor;
        }
    }
    
    hdr->free_space_upper_bound = write_cursor;
    
    // Optionally clean up trailing deleted slots to reclaim slot directory space
    while (hdr->slot_count > 0 && !slots[hdr->slot_count - 1].is_live()) {
        hdr->slot_count--;
        hdr->free_space_lower_bound -= static_cast<std::uint32_t>(sizeof(Slot));
    }
    
    m_handle.mark_dirty();
    return {};
}

// Removed duplicate page_size

void TablePage::set_lsn(wal::LSN lsn) {
    auto hdr = m_handle.get().read_header();
    hdr.lsn = lsn.value();
    m_handle.get_mut().write_header(hdr);
}

wal::LSN TablePage::lsn() const {
    return wal::LSN(m_handle.get().read_header().lsn);
}

} // namespace nemo::storage
