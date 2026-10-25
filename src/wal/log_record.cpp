#include "nemo/wal/log_record.hpp"
#include <cstring>
#include <stdexcept>

namespace nemo::wal {

// A simple CRC32 implementation for our WAL checksums
static std::uint32_t crc32(const std::uint8_t* buf, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFF;
    for (std::size_t i = 0; i < size; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
    }
    return ~crc;
}

void LogRecord::calculate_checksum() {
    std::vector<std::uint8_t> temp;
    temp.reserve(HEADER_SIZE - 4 + m_payload.size()); // Exclude checksum field
    
    // Serialize everything except the checksum
    std::uint64_t lsn_val = m_lsn.value();
    std::uint64_t prev_lsn_val = m_prev_lsn.value();
    std::uint64_t undo_next_lsn_val = m_undo_next_lsn.value();
    std::uint32_t payload_len = get_payload_size();
    
    // We append directly for checksum calculation
    const auto append = [&temp](const void* src, std::size_t len) {
        const std::uint8_t* bytes = static_cast<const std::uint8_t*>(src);
        temp.insert(temp.end(), bytes, bytes + len);
    };
    
    append(&lsn_val, sizeof(lsn_val));
    append(&m_txn_id, sizeof(m_txn_id));
    append(&prev_lsn_val, sizeof(prev_lsn_val));
    append(&undo_next_lsn_val, sizeof(undo_next_lsn_val));
    append(&m_type, sizeof(m_type));
    append(&payload_len, sizeof(payload_len));
    
    if (payload_len > 0) {
        append(m_payload.data(), payload_len);
    }
    
    m_checksum = crc32(temp.data(), temp.size());
}

bool LogRecord::verify_checksum() const {
    LogRecord copy = *this;
    copy.calculate_checksum();
    return m_checksum == copy.get_checksum();
}

void LogRecord::serialize(std::vector<std::uint8_t>& out) const {
    // Ensure checksum is up to date
    const_cast<LogRecord*>(this)->calculate_checksum();
    
    std::uint64_t lsn_val = m_lsn.value();
    std::uint64_t prev_lsn_val = m_prev_lsn.value();
    std::uint64_t undo_next_lsn_val = m_undo_next_lsn.value();
    std::uint32_t payload_len = get_payload_size();
    
    const auto append = [&out](const void* src, std::size_t len) {
        const std::uint8_t* bytes = static_cast<const std::uint8_t*>(src);
        out.insert(out.end(), bytes, bytes + len);
    };
    
    append(&lsn_val, sizeof(lsn_val));
    append(&m_txn_id, sizeof(m_txn_id));
    append(&prev_lsn_val, sizeof(prev_lsn_val));
    append(&undo_next_lsn_val, sizeof(undo_next_lsn_val));
    append(&m_type, sizeof(m_type));
    append(&payload_len, sizeof(payload_len));
    append(&m_checksum, sizeof(m_checksum));
    
    if (payload_len > 0) {
        append(m_payload.data(), payload_len);
    }
}

std::size_t LogRecord::deserialize(const std::uint8_t* data, std::size_t size) {
    if (size < HEADER_SIZE) return 0;
    
    const std::uint8_t* ptr = data;
    const auto read = [&ptr](void* dst, std::size_t len) {
        std::memcpy(dst, ptr, len);
        ptr += len;
    };
    
    std::uint64_t lsn_val, prev_lsn_val, undo_next_lsn_val;
    std::uint32_t payload_len;
    
    read(&lsn_val, sizeof(lsn_val));
    read(&m_txn_id, sizeof(m_txn_id));
    read(&prev_lsn_val, sizeof(prev_lsn_val));
    read(&undo_next_lsn_val, sizeof(undo_next_lsn_val));
    read(&m_type, sizeof(m_type));
    read(&payload_len, sizeof(payload_len));
    read(&m_checksum, sizeof(m_checksum));
    
    m_lsn = LSN(lsn_val);
    m_prev_lsn = LSN(prev_lsn_val);
    m_undo_next_lsn = LSN(undo_next_lsn_val);
    
    if (size < HEADER_SIZE + payload_len) {
        return 0; // Incomplete record
    }
    
    m_payload.resize(payload_len);
    if (payload_len > 0) {
        read(m_payload.data(), payload_len);
    }
    
    return HEADER_SIZE + payload_len;
}

} // namespace nemo::wal
