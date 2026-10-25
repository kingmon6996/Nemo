#include "nemo/storage/table_heap.hpp"
#include "nemo/wal/wal_manager.hpp"
#include "nemo/transaction/transaction_id.hpp"
#include "nemo/transaction/undo_record.hpp"
#include <cstring>
#include <vector>
#include <cstddef>
#include "nemo/storage/record_header.hpp"
#include "nemo/storage/slot.hpp"

namespace nemo::storage {

TableHeap::TableHeap(BufferPool* buffer_pool, PageID first_page_id) 
    : m_buffer_pool(buffer_pool), m_first_page_id(first_page_id) {}

Result<TableHeap> TableHeap::create(BufferPool* buffer_pool) {
    auto handle_res = buffer_pool->allocate_page();
    if (!handle_res) return handle_res.error();
    
    TablePage page(std::move(handle_res.value()));
    page.init();
    
    return TableHeap(buffer_pool, page.page_id());
}

Result<RecordID> TableHeap::insert(const Record& record, const TupleLayout& layout, transaction::TransactionContext& ctx) {
    auto bytes = RecordSerializer::serialize(record, layout);
    
    // Set MVCC creation identity
    if (bytes.size() >= sizeof(RecordHeader)) {
        auto* hdr = reinterpret_cast<RecordHeader*>(bytes.data());
        hdr->mvcc.xmin = ctx.get_transaction()->get_id();
        hdr->mvcc.xmax = transaction::INVALID_TRANSACTION_ID;
        hdr->mvcc.prev_version = RecordID{};
    }
    
    // For V1, simple linear scan to find space.
    PageID current_id = m_first_page_id;
    PageID last_id{};
    
    while (current_id.is_valid()) {
        auto handle_res = m_buffer_pool->fetch_page(current_id);
        if (!handle_res) return handle_res.error();
        
        TablePage page(std::move(handle_res.value()));
        
        if (page.free_space() < bytes.size() + sizeof(Slot)) {
            page.compact(); // try compacting
        }
        
        auto insert_res = page.insert(bytes);
        if (insert_res) {
            return insert_res.value(); // Success
        }
        
        // Not enough space, move to next page
        last_id = current_id;
        current_id = page.next_page_id();
    }
    
    // Exhausted existing pages, need to allocate a new one
    auto new_handle_res = m_buffer_pool->allocate_page();
    if (!new_handle_res) return new_handle_res.error();
    
    TablePage new_page(std::move(new_handle_res.value()));
    new_page.init();
    new_page.set_prev_page_id(last_id);
    
    auto insert_res = new_page.insert(bytes);
    if (!insert_res) return insert_res.error();
    
    // WAL Logging
    if (m_wal_manager) {
        std::vector<std::uint8_t> payload(4); // Just PageID for physical insert mockup
        std::uint32_t raw_page_id = static_cast<std::uint32_t>(new_page.page_id().value());
        std::memcpy(payload.data(), &raw_page_id, 4);
        
        wal::LogRecord log_rec(ctx.get_transaction()->get_id(), wal::LogRecordType::Insert, wal::LSN(ctx.get_transaction()->get_last_lsn()), wal::LSN::invalid(), payload);
        wal::LSN lsn = m_wal_manager->append(log_rec);
        ctx.get_transaction()->set_last_lsn(lsn.value());
        new_page.set_lsn(lsn);
    }
    
    // Add to Undo tracking
    ctx.get_transaction()->add_undo_record({transaction::UndoOperation::Insert, TableID(0), insert_res.value()});
    
    // Link previous to new
    if (last_id.is_valid()) {
        auto last_handle_res = m_buffer_pool->fetch_page(last_id);
        if (last_handle_res) {
            TablePage last_page(std::move(last_handle_res.value()));
            last_page.set_next_page_id(new_page.page_id());
        }
    }
    
    return insert_res.value();
}

Result<Record> TableHeap::get_physical(const RecordID& id, const TupleLayout& layout) {
    auto handle_res = m_buffer_pool->fetch_page(id.page_id());
    if (!handle_res) return handle_res.error();
    
    TablePage page(std::move(handle_res.value()));
    auto get_res = page.get(id);
    if (!get_res) return get_res.error();
    
    return RecordDeserializer::deserialize(get_res.value(), layout);
}

Result<Record> TableHeap::get(const RecordID& id, const TupleLayout& layout, transaction::TransactionContext& ctx, const transaction::VisibilityManager& visibility_mgr) {
    RecordID current_id = id;
    
    // Version chain traversal (assuming old-to-new is what indexes point to - wait, usually indexes point to the NEWEST version in an update chain if we do out-of-place updates without updating the index immediately. But here we said UPDATE deletes old and inserts new. The index must be updated).
    // Let's assume the provided RecordID is the physical head.
    while (current_id.is_valid()) {
        auto handle_res = m_buffer_pool->fetch_page(current_id.page_id());
        if (!handle_res) return handle_res.error();
        
        TablePage page(std::move(handle_res.value()));
        auto get_res = page.get(current_id);
        if (!get_res) return get_res.error();
        
        if (get_res.value().size() >= sizeof(RecordHeader)) {
            const auto* hdr = reinterpret_cast<const RecordHeader*>(get_res.value().data());
            
            if (visibility_mgr.is_visible(hdr->mvcc, ctx.get_snapshot(), ctx.get_transaction()->get_id())) {
                return RecordDeserializer::deserialize(get_res.value(), layout);
            }
            // Move to previous version (Old-to-New linking model doesn't work well if index points to new. If index points to oldest, we traverse forward).
            // We use prev_version pointer (New-to-Old) here.
            current_id = hdr->mvcc.prev_version;
        } else {
            return nemo::Status::InternalError; // Corrupted
        }
    }
    
    return nemo::Status::NotFound; // No visible version found
}

Result<void> TableHeap::update(const RecordID& id, const Record& record, const TupleLayout& layout, transaction::TransactionContext& ctx) {
    // 1. Read old version bytes
    auto handle_res = m_buffer_pool->fetch_page(id.page_id());
    if (!handle_res) return handle_res.error();
    
    TablePage page(std::move(handle_res.value()));
    auto get_res = page.get(id);
    if (!get_res) return get_res.error();
    
    std::vector<std::byte> old_bytes(get_res.value().begin(), get_res.value().end());
    if (old_bytes.size() < sizeof(RecordHeader)) return nemo::Status::InternalError;
    
    auto* old_hdr = reinterpret_cast<RecordHeader*>(old_bytes.data());
    
    // Check write-write conflict (simplistic for Mod 9)
    if (old_hdr->mvcc.xmax != transaction::INVALID_TRANSACTION_ID && old_hdr->mvcc.xmax != ctx.get_transaction()->get_id()) {
        // Another txn has modified this!
        return nemo::Status::TransactionAborted;
    }
    
    // Mark old version as deleted by current txn
    old_hdr->mvcc.xmax = ctx.get_transaction()->get_id();
    auto update_res = page.update(id, old_bytes);
    if (!update_res) return update_res.error();
    
    // WAL Logging for Update (delete old version part)
    if (m_wal_manager) {
        std::vector<std::uint8_t> payload(4);
        std::uint32_t raw_page_id = static_cast<std::uint32_t>(page.page_id().value());
        std::memcpy(payload.data(), &raw_page_id, 4);
        
        wal::LogRecord log_rec(ctx.get_transaction()->get_id(), wal::LogRecordType::Update, wal::LSN(ctx.get_transaction()->get_last_lsn()), wal::LSN::invalid(), payload);
        wal::LSN lsn = m_wal_manager->append(log_rec);
        ctx.get_transaction()->set_last_lsn(lsn.value());
        page.set_lsn(lsn);
    }
    
    // Add to Undo tracking for old record (to un-delete it if we abort)
    ctx.get_transaction()->add_undo_record({transaction::UndoOperation::Delete, TableID(0), id});
    
    // 2. Insert new version
    auto new_bytes = RecordSerializer::serialize(record, layout);
    if (new_bytes.size() >= sizeof(RecordHeader)) {
        auto* new_hdr = reinterpret_cast<RecordHeader*>(new_bytes.data());
        new_hdr->mvcc.xmin = ctx.get_transaction()->get_id();
        new_hdr->mvcc.xmax = transaction::INVALID_TRANSACTION_ID;
        new_hdr->mvcc.prev_version = id; // New version points back to Old version
    }
    
    // Use physical insert bypassing MVCC context since we manually configured the header
    // But since insert() automatically sets xmin, we must either refactor insert or just call page.insert
    
    // We can just call our own `insert` with the context, and it will overwrite xmin, but overwrite prev_version to invalid. 
    // Let's refactor:
    
    // We will do a raw insert loop similar to `insert` method for the new bytes
    // (In a full implementation this would be abstracted, doing it inline for brevity of this module)
    PageID current_id = m_first_page_id;
    while (current_id.is_valid()) {
        auto ins_handle_res = m_buffer_pool->fetch_page(current_id);
        if (!ins_handle_res) return ins_handle_res.error();
        TablePage ins_page(std::move(ins_handle_res.value()));
        auto raw_ins_res = ins_page.insert(new_bytes);
        if (raw_ins_res) {
            if (m_wal_manager) {
                std::vector<std::uint8_t> payload(4);
                std::uint32_t raw_page_id = static_cast<std::uint32_t>(ins_page.page_id().value());
                std::memcpy(payload.data(), &raw_page_id, 4);
                
                wal::LogRecord log_rec(ctx.get_transaction()->get_id(), wal::LogRecordType::Insert, wal::LSN(ctx.get_transaction()->get_last_lsn()), wal::LSN::invalid(), payload);
                wal::LSN lsn = m_wal_manager->append(log_rec);
                ctx.get_transaction()->set_last_lsn(lsn.value());
                ins_page.set_lsn(lsn);
            }
            ctx.get_transaction()->add_undo_record({transaction::UndoOperation::Insert, TableID(0), raw_ins_res.value()});
            return {};
        }
        current_id = ins_page.next_page_id();
    }
    
    return nemo::Status::InternalError; // Simplified error handling
}

Result<void> TableHeap::erase(const RecordID& id, transaction::TransactionContext& ctx) {
    auto handle_res = m_buffer_pool->fetch_page(id.page_id());
    if (!handle_res) return handle_res.error();
    
    TablePage page(std::move(handle_res.value()));
    auto get_res = page.get(id);
    if (!get_res) return get_res.error();
    
    std::vector<std::byte> old_bytes(get_res.value().begin(), get_res.value().end());
    if (old_bytes.size() < sizeof(RecordHeader)) return nemo::Status::InternalError;
    
    auto* old_hdr = reinterpret_cast<RecordHeader*>(old_bytes.data());
    
    // Write conflict check
    if (old_hdr->mvcc.xmax != transaction::INVALID_TRANSACTION_ID && old_hdr->mvcc.xmax != ctx.get_transaction()->get_id()) {
        return nemo::Status::TransactionAborted;
    }
    
    old_hdr->mvcc.xmax = ctx.get_transaction()->get_id();
    auto update_res = page.update(id, old_bytes);
    if (!update_res) return update_res.error();
    
    if (m_wal_manager) {
        std::vector<std::uint8_t> payload(4);
        std::uint32_t raw_page_id = static_cast<std::uint32_t>(page.page_id().value());
        std::memcpy(payload.data(), &raw_page_id, 4);
        
        wal::LogRecord log_rec(ctx.get_transaction()->get_id(), wal::LogRecordType::Delete, wal::LSN(ctx.get_transaction()->get_last_lsn()), wal::LSN::invalid(), payload);
        wal::LSN lsn = m_wal_manager->append(log_rec);
        ctx.get_transaction()->set_last_lsn(lsn.value());
        page.set_lsn(lsn);
    }
    
    ctx.get_transaction()->add_undo_record({transaction::UndoOperation::Delete, TableID(0), id});
    
    return {};
}

} // namespace nemo::storage
