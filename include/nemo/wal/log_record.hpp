#ifndef NEMO_WAL_LOG_RECORD_HPP
#define NEMO_WAL_LOG_RECORD_HPP

#include "nemo/wal/lsn.hpp"
#include "nemo/wal/log_record_type.hpp"
#include "nemo/transaction/transaction_id.hpp"
#include <vector>
#include <cstdint>
#include <string>

namespace nemo::wal {

// A memory representation of a Log Record.
class LogRecord {
public:
    // Header size: LSN(8) + TxnID(8) + PrevLSN(8) + UndoNextLSN(8) + Type(1) + PayloadLength(4) + Checksum(4) = 41 bytes
    static constexpr std::size_t HEADER_SIZE = 41;

    LogRecord() = default;

    // Transaction state constructor
    LogRecord(transaction::TransactionID txn_id, LogRecordType type, LSN prev_lsn)
        : m_txn_id(txn_id), m_prev_lsn(prev_lsn), m_type(type) {}

    // Generic physical record constructor
    LogRecord(transaction::TransactionID txn_id, LogRecordType type, LSN prev_lsn, std::vector<std::uint8_t> payload)
        : m_txn_id(txn_id), m_prev_lsn(prev_lsn), m_type(type), m_payload(std::move(payload)) {}

    // CLR constructor
    LogRecord(transaction::TransactionID txn_id, LogRecordType type, LSN prev_lsn, LSN undo_next_lsn, std::vector<std::uint8_t> payload)
        : m_txn_id(txn_id), m_prev_lsn(prev_lsn), m_undo_next_lsn(undo_next_lsn), m_type(type), m_payload(std::move(payload)) {}

    LSN get_lsn() const { return m_lsn; }
    void set_lsn(LSN lsn) { m_lsn = lsn; }

    transaction::TransactionID get_txn_id() const { return m_txn_id; }
    LogRecordType get_type() const { return m_type; }
    
    LSN get_prev_lsn() const { return m_prev_lsn; }
    LSN get_undo_next_lsn() const { return m_undo_next_lsn; } // Only valid for CLR

    const std::vector<std::uint8_t>& get_payload() const { return m_payload; }
    std::uint32_t get_payload_size() const { return static_cast<std::uint32_t>(m_payload.size()); }
    
    std::uint32_t get_size() const { return HEADER_SIZE + get_payload_size(); }

    std::uint32_t get_checksum() const { return m_checksum; }
    void calculate_checksum();
    bool verify_checksum() const;

    // Serialization
    void serialize(std::vector<std::uint8_t>& out) const;
    
    // Deserialization from a buffer (assumes buffer has exactly the expected size or more)
    // Returns the number of bytes read, or 0 if malformed
    std::size_t deserialize(const std::uint8_t* data, std::size_t size);

private:
    LSN m_lsn{LSN::invalid()};
    transaction::TransactionID m_txn_id{0};
    LSN m_prev_lsn{LSN::invalid()};
    LSN m_undo_next_lsn{LSN::invalid()}; // Used for CLRs
    LogRecordType m_type{LogRecordType::Invalid};
    std::vector<std::uint8_t> m_payload;
    std::uint32_t m_checksum{0};
};

} // namespace nemo::wal

#endif // NEMO_WAL_LOG_RECORD_HPP
