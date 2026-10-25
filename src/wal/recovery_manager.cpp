#include "nemo/wal/recovery_manager.hpp"
#include "nemo/wal/log_reader.hpp"
#include "nemo/storage/page_handle.hpp"
#include <iostream>
#include <queue>
#include <cstring>

namespace nemo::wal {

RecoveryManager::RecoveryManager(WALManager* wal, CheckpointManager* checkpoint, storage::BufferPool* buffer_pool, const std::string& wal_dir)
    : m_wal_manager(wal), m_checkpoint_manager(checkpoint), m_buffer_pool(buffer_pool), m_wal_dir(wal_dir) {}

bool RecoveryManager::recover() {
    m_state = RecoveryState::Analysis;
    if (!phase_analysis()) {
        m_state = RecoveryState::Failed;
        return false;
    }
    
    m_state = RecoveryState::Redo;
    if (!phase_redo()) {
        m_state = RecoveryState::Failed;
        return false;
    }
    
    m_state = RecoveryState::Undo;
    if (!phase_undo()) {
        m_state = RecoveryState::Failed;
        return false;
    }
    
    m_state = RecoveryState::Complete;
    return true;
}

bool RecoveryManager::phase_analysis() {
    // 1. Read Master Record to find latest Checkpoint
    auto checkpoint_meta = m_checkpoint_manager->read_master_record();
    
    LogReader reader(m_wal_dir);
    if (!reader.initialize()) return true; // Empty WAL, nothing to recover
    
    if (checkpoint_meta) {
        if (!reader.seek_to_lsn(checkpoint_meta->checkpoint_lsn)) {
            // Checkpoint LSN not found. Start from beginning.
            reader.seek_to_first();
        }
    } else {
        reader.seek_to_first();
    }
    
    // 2. Scan forward, building DPT and TT
    while (auto record_opt = reader.read_next()) {
        const LogRecord& record = *record_opt;
        LSN lsn = record.get_lsn();
        transaction::TransactionID txn_id = record.get_txn_id();
        
        switch (record.get_type()) {
            case LogRecordType::TxnBegin:
                m_txn_table.update(txn_id, transaction::TransactionState::Active, lsn);
                break;
            case LogRecordType::TxnCommit:
                m_txn_table.update(txn_id, transaction::TransactionState::Committed, lsn);
                break;
            case LogRecordType::TxnAbort:
                m_txn_table.update(txn_id, transaction::TransactionState::Aborted, lsn);
                break;
            case LogRecordType::TxnEnd:
                m_txn_table.remove(txn_id);
                break;
            case LogRecordType::Insert:
            case LogRecordType::Update:
            case LogRecordType::Delete:
            case LogRecordType::CLR: {
                m_txn_table.update(txn_id, transaction::TransactionState::Active, lsn);
                
                // For physical records, assume payload starts with PageID (4 bytes)
                if (record.get_payload_size() >= 4) {
                    PageID page_id(*reinterpret_cast<const std::uint32_t*>(record.get_payload().data()));
                    m_dirty_page_table.update(page_id, lsn); // recLSN is the first LSN to dirty it
                }
                break;
            }
            default:
                break;
        }
    }
    
    // 3. Determine Redo Start Point
    m_redo_start_lsn = m_dirty_page_table.get_redo_start_lsn();
    if (!m_redo_start_lsn.is_valid() && checkpoint_meta) {
        m_redo_start_lsn = checkpoint_meta->checkpoint_lsn;
    }
    
    return true;
}

bool RecoveryManager::phase_redo() {
    if (!m_redo_start_lsn.is_valid()) return true; // Nothing to redo
    
    LogReader reader(m_wal_dir);
    if (!reader.initialize()) return false;
    
    if (!reader.seek_to_lsn(m_redo_start_lsn)) {
        // The start LSN might have been from an older segment that was truncated, fallback to first
        reader.seek_to_first(); 
    }
    
    while (auto record_opt = reader.read_next()) {
        const LogRecord& record = *record_opt;
        if (record.get_type() == LogRecordType::Insert ||
            record.get_type() == LogRecordType::Update ||
            record.get_type() == LogRecordType::Delete ||
            record.get_type() == LogRecordType::CLR) {
            
            redo_record(record);
        }
    }
    
    return true;
}

void RecoveryManager::redo_record(const LogRecord& record) {
    if (record.get_payload_size() < 4) return;
    
    PageID page_id(*reinterpret_cast<const std::uint32_t*>(record.get_payload().data()));
    
    // 1. Is page in DPT?
    if (!m_dirty_page_table.contains(page_id)) return;
    
    // 2. Is recLSN > record LSN?
    if (m_dirty_page_table.get_rec_lsn(page_id).value() > record.get_lsn().value()) return;
    
    // 3. Fetch Page & check pageLSN (Idempotence)
    auto handle_res = m_buffer_pool->fetch_page(page_id);
    if (!handle_res) return; // DB Error
    
    auto& handle = handle_res.value();
    if (handle.get().read_header().lsn >= record.get_lsn().value()) {
        return; // Already applied
    }
    
    // 4. REDO application
    // Payload Format (Update): [PageID:4] [Offset:2] [Length:2] [AfterImage:...]
    // (For Module 11 simplicity, we assume an exact physical byte-overwrite payload)
    if (record.get_type() == LogRecordType::Update && record.get_payload_size() >= 8) {
        const std::uint8_t* payload = record.get_payload().data();
        std::uint16_t offset = *reinterpret_cast<const std::uint16_t*>(payload + 4);
        std::uint16_t length = *reinterpret_cast<const std::uint16_t*>(payload + 6);
        
        if (record.get_payload_size() >= 8u + length) {
            std::memcpy(handle.get_mut().data().data() + offset, payload + 8, length);
            auto hdr = handle.get_mut().read_header();
            hdr.lsn = record.get_lsn().value();
            handle.get_mut().write_header(hdr);
            handle.mark_dirty();
        }
    }
}

bool RecoveryManager::phase_undo() {
    auto losers = m_txn_table.get_losers();
    if (losers.empty()) return true;
    
    // Priority queue of LSNs to undo, largest first
    std::priority_queue<std::uint64_t> undo_pq;
    
    for (const auto& loser : losers) {
        if (loser.undo_next_lsn.is_valid()) {
            undo_pq.push(loser.undo_next_lsn.value());
        }
    }
    
    LogReader reader(m_wal_dir);
    if (!reader.initialize()) return false;
    
    while (!undo_pq.empty()) {
        std::uint64_t target_lsn_val = undo_pq.top();
        undo_pq.pop();
        
        if (!reader.seek_to_lsn(LSN(target_lsn_val))) continue;
        auto record_opt = reader.read_next();
        if (!record_opt || record_opt->get_lsn().value() != target_lsn_val) continue;
        
        const LogRecord& record = *record_opt;
        
        if (record.get_type() == LogRecordType::Update) {
            undo_record(record);
            
            // Queue next LSN for this transaction
            if (record.get_prev_lsn().is_valid()) {
                undo_pq.push(record.get_prev_lsn().value());
            }
        }
    }
    
    // Write abort/end records for losers
    for (const auto& loser : losers) {
        LogRecord end_rec(loser.txn_id, LogRecordType::TxnEnd, LSN::invalid());
        m_wal_manager->append(end_rec);
    }
    m_wal_manager->flush();
    
    return true;
}

void RecoveryManager::undo_record(const LogRecord& record) {
    if (record.get_payload_size() < 4) return;
    PageID page_id(*reinterpret_cast<const std::uint32_t*>(record.get_payload().data()));
    
    auto handle_res = m_buffer_pool->fetch_page(page_id);
    if (!handle_res) return; 
    
    auto& handle = handle_res.value();
    
    // For Undo Update: [PageID:4] [Offset:2] [Length:2] [BeforeImage:Length] [AfterImage:Length]
    if (record.get_type() == LogRecordType::Update) {
        // Determine payload boundaries based on the assumed structure where Before and After are packed
        // We'll assume for simplicity: [4] [2] [2] [Before] [After]
        const std::uint8_t* payload = record.get_payload().data();
        std::uint16_t offset = *reinterpret_cast<const std::uint16_t*>(payload + 4);
        std::uint16_t length = *reinterpret_cast<const std::uint16_t*>(payload + 6);
        
        if (record.get_payload_size() >= 8u + length * 2u) {
            // Apply Before Image
            std::memcpy(handle.get_mut().data().data() + offset, payload + 8, length);
            
            // Generate CLR
            std::vector<std::uint8_t> clr_payload;
            // A CLR for an Update basically becomes a redo-only Update (setting it back to before-image)
            clr_payload.insert(clr_payload.end(), payload, payload + 8); // Header info
            clr_payload.insert(clr_payload.end(), payload + 8, payload + 8 + length); // New "After" is the old "Before"
            
            LogRecord clr(record.get_txn_id(), LogRecordType::CLR, LSN::invalid(), record.get_prev_lsn(), clr_payload);
            LSN clr_lsn = m_wal_manager->append(clr);
            
            auto hdr = handle.get_mut().read_header();
            hdr.lsn = clr_lsn.value();
            handle.get_mut().write_header(hdr);
            handle.mark_dirty();
        }
    }
}

} // namespace nemo::wal
