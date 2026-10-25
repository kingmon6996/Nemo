#include "nemo/wal/checkpoint_manager.hpp"
#include <fstream>
#include <chrono>

namespace nemo::wal {

CheckpointManager::CheckpointManager(WALManager* wal, transaction::TransactionManager* txn_mgr, storage::BufferPool* buffer_pool, const std::string& db_dir)
    : m_wal_manager(wal), m_txn_manager(txn_mgr), m_buffer_pool(buffer_pool) {
    m_master_record_path = db_dir + "/master.nm";
}

bool CheckpointManager::perform_checkpoint() {
    // 1. Write CHECKPOINT_BEGIN
    LogRecord begin_rec(0, LogRecordType::CheckpointBegin, LSN::invalid());
    LSN begin_lsn = m_wal_manager->append(begin_rec);
    
    // 2. Extract state (In a fuzzy checkpoint, we don't lock everything. We'd grab a consistent snapshot of the TransactionTable and DirtyPageTable).
    // For Module 11 simplicity, we assume the TransactionManager and BufferPool provide this snapshot.
    // We mock the payload generation here.
    std::vector<std::uint8_t> end_payload;
    // ... Serialize active txns and dirty pages into end_payload ...
    
    // 3. Write CHECKPOINT_END
    LogRecord end_rec(0, LogRecordType::CheckpointEnd, begin_lsn, end_payload);
    LSN end_lsn = m_wal_manager->append(end_rec);
    
    // 4. Force WAL durable
    m_wal_manager->flush_up_to(end_lsn);
    
    // 5. Update Master Record
    CheckpointMetadata meta;
    meta.checkpoint_lsn = begin_lsn; // Recovery starts analysis from CHECKPOINT_BEGIN
    meta.timestamp = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    
    return write_master_record(meta);
}

bool CheckpointManager::write_master_record(const CheckpointMetadata& metadata) {
    std::ofstream ofs(m_master_record_path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    
    std::uint64_t lsn_val = metadata.checkpoint_lsn.value();
    ofs.write(reinterpret_cast<const char*>(&lsn_val), sizeof(lsn_val));
    ofs.write(reinterpret_cast<const char*>(&metadata.timestamp), sizeof(metadata.timestamp));
    
    ofs.flush();
    return true;
}

std::optional<CheckpointMetadata> CheckpointManager::read_master_record() {
    std::ifstream ifs(m_master_record_path, std::ios::binary);
    if (!ifs.is_open()) return std::nullopt;
    
    CheckpointMetadata meta;
    std::uint64_t lsn_val;
    ifs.read(reinterpret_cast<char*>(&lsn_val), sizeof(lsn_val));
    ifs.read(reinterpret_cast<char*>(&meta.timestamp), sizeof(meta.timestamp));
    
    if (ifs.gcount() != static_cast<std::streamsize>(sizeof(meta.timestamp))) return std::nullopt;
    
    meta.checkpoint_lsn = LSN(lsn_val);
    return meta;
}

} // namespace nemo::wal
