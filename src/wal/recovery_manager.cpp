#include "nemo/wal/recovery_manager.hpp"
#include "nemo/wal/log_reader.hpp"
#include "nemo/storage/checksum.hpp"
#include "nemo/storage/page.hpp"
#include "nemo/storage/page_handle.hpp"
#include "nemo/storage/page_header.hpp"
#include "nemo/logging/logger.hpp"
#include <algorithm>
#include <cstring>
#include <queue>
#include <vector>

namespace nemo::wal {
namespace {

constexpr std::size_t PAGE_IMAGE_HEADER_SIZE = sizeof(std::uint64_t) + sizeof(std::uint32_t);

std::uint32_t read_u32(const std::uint8_t* bytes) {
    std::uint32_t value = 0;
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(bytes[shift / 8]) << shift;
    }
    return value;
}

std::uint64_t read_u64(const std::uint8_t* bytes) {
    std::uint64_t value = 0;
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes[shift / 8]) << shift;
    }
    return value;
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
    }
}

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
    }
}

struct PageImageView {
    PageID page_id{0};
    std::uint32_t page_size{0};
    const std::uint8_t* before{nullptr};
    const std::uint8_t* after{nullptr};
};

bool parse_page_image(const LogRecord& record, PageImageView& image) {
    const auto& payload = record.get_payload();
    if (payload.size() < PAGE_IMAGE_HEADER_SIZE) return false;

    image.page_id = PageID(read_u64(payload.data()));
    image.page_size = read_u32(payload.data() + sizeof(std::uint64_t));
    if (image.page_size == 0 ||
        payload.size() != PAGE_IMAGE_HEADER_SIZE +
            static_cast<std::size_t>(image.page_size) * 2) {
        return false;
    }
    image.before = payload.data() + PAGE_IMAGE_HEADER_SIZE;
    image.after = image.before + image.page_size;
    return true;
}

std::vector<std::uint8_t> make_page_image_payload(
    PageID page_id, std::uint32_t page_size,
    const std::uint8_t* before, const std::uint8_t* after) {
    std::vector<std::uint8_t> payload;
    payload.reserve(PAGE_IMAGE_HEADER_SIZE + static_cast<std::size_t>(page_size) * 2);
    append_u64(payload, page_id.value());
    append_u32(payload, page_size);
    payload.insert(payload.end(), before, before + page_size);
    payload.insert(payload.end(), after, after + page_size);
    return payload;
}

} // namespace

RecoveryManager::RecoveryManager(WALManager* wal, CheckpointManager* checkpoint,
                                 storage::BufferPool* buffer_pool,
                                 const std::string& wal_dir)
    : m_wal_manager(wal), m_checkpoint_manager(checkpoint),
      m_buffer_pool(buffer_pool), m_wal_dir(wal_dir) {}

bool RecoveryManager::recover() {
    m_transactions.clear();
    m_state = RecoveryState::Analysis;
    if (!phase_analysis()) {
        NEMO_LOG_ERROR("Recovery", "WAL analysis failed.");
        m_state = RecoveryState::Failed;
        return false;
    }

    m_state = RecoveryState::Redo;
    if (!phase_redo()) {
        NEMO_LOG_ERROR("Recovery", "WAL redo failed.");
        m_state = RecoveryState::Failed;
        return false;
    }

    m_state = RecoveryState::Undo;
    if (!phase_undo()) {
        NEMO_LOG_ERROR("Recovery", "WAL undo failed.");
        m_state = RecoveryState::Failed;
        return false;
    }

    auto reload_result = m_buffer_pool->disk_manager()->reload_after_recovery();
    if (!reload_result) {
        NEMO_LOG_ERROR("Recovery", "Failed to reload database header after recovery.");
        m_state = RecoveryState::Failed;
        return false;
    }
    auto flush_result = m_buffer_pool->disk_manager()->flush();
    if (!flush_result) {
        NEMO_LOG_ERROR("Recovery", "Failed to flush recovered database state.");
        m_state = RecoveryState::Failed;
        return false;
    }
    m_state = RecoveryState::Complete;
    return true;
}

bool RecoveryManager::phase_analysis() {
    m_dirty_page_table.clear();
    m_analysis_start_lsn = LSN::invalid();
    m_redo_start_lsn = LSN::invalid();

    const auto master_record = m_checkpoint_manager->read_master_record();
    if (m_checkpoint_manager->has_master_record() && !master_record) {
        NEMO_LOG_ERROR("Recovery", "Master checkpoint record is invalid.");
        return false;
    }

    std::optional<CheckpointState> checkpoint_state;
    if (master_record && !master_record->legacy) {
        checkpoint_state = m_checkpoint_manager->read_checkpoint_state(
            master_record->checkpoint_lsn);
        if (!checkpoint_state) {
            NEMO_LOG_ERROR("Recovery", "Checkpoint state is missing or invalid.");
            return false;
        }
        m_analysis_start_lsn = master_record->checkpoint_lsn;
        m_redo_start_lsn = master_record->checkpoint_lsn;
        for (const auto& page : checkpoint_state->dirty_pages) {
            auto it = m_dirty_page_table.find(page.page_id);
            if (it == m_dirty_page_table.end()) {
                m_dirty_page_table.emplace(page.page_id, LSN(page.rec_lsn));
            } else if (it->second.value() < page.rec_lsn) {
                it->second = LSN(page.rec_lsn);
            }
            if (m_redo_start_lsn.value() == master_record->checkpoint_lsn.value() ||
                page.rec_lsn < m_redo_start_lsn.value()) {
                m_redo_start_lsn = LSN(page.rec_lsn);
            }
        }
        for (const auto& txn : checkpoint_state->transactions) {
            m_transactions[txn.transaction_id] = {
                LSN(txn.first_lsn), LSN(txn.last_lsn), false, false};
        }
    }

    LogReader reader(m_wal_dir);
    if (!reader.initialize()) {
        if (m_analysis_start_lsn.is_valid()) {
            NEMO_LOG_ERROR(
                "Recovery", "Checkpoint points to a missing WAL segment.");
            return false;
        }
        return !master_record.has_value();
    }
    const bool seeked = m_analysis_start_lsn.is_valid()
        ? reader.seek_to_lsn(m_analysis_start_lsn)
        : reader.seek_to_first();
    if (!seeked) {
        if (!m_analysis_start_lsn.is_valid() &&
            m_wal_manager->get_flushed_lsn().value() == 0) {
            return true;
        }
        NEMO_LOG_ERROR("Recovery", "Unable to seek to the recovery start LSN.");
        return false;
    }

    while (auto record_opt = reader.read_next()) {
        const auto& record = *record_opt;
        if (m_analysis_start_lsn.is_valid() &&
            record.get_lsn().value() == m_analysis_start_lsn.value() &&
            record.get_type() != LogRecordType::CheckpointBegin) {
            NEMO_LOG_ERROR("Recovery", "Master checkpoint LSN is not a begin record.");
            return false;
        }

        if (record.get_type() == LogRecordType::CheckpointEnd) {
            const auto state =
                deserialize_checkpoint_state(record.get_payload());
            if (!state && master_record &&
                !master_record->legacy) {
                NEMO_LOG_ERROR(
                    "Recovery", "Checkpoint-end record has an invalid payload.");
                return false;
            }
            if (!state) continue;
            m_transactions.clear();
            for (const auto& txn : state->transactions) {
                m_transactions[txn.transaction_id] = {
                    LSN(txn.first_lsn), LSN(txn.last_lsn), false, false};
            }
            m_dirty_page_table.clear();
            for (const auto& page : state->dirty_pages) {
                auto it = m_dirty_page_table.find(page.page_id);
                if (it == m_dirty_page_table.end()) {
                    m_dirty_page_table.emplace(page.page_id, LSN(page.rec_lsn));
                } else if (it->second.value() < page.rec_lsn) {
                    it->second = LSN(page.rec_lsn);
                }
            }
            continue;
        }

        const auto txn_id = record.get_txn_id();
        if (txn_id != 0) {
            auto& state = m_transactions[txn_id];
            if (!state.first_lsn.is_valid() ||
                record.get_type() == LogRecordType::TxnBegin) {
                state.first_lsn = record.get_lsn();
            }
            state.last_lsn = record.get_lsn();
            switch (record.get_type()) {
                case LogRecordType::TxnCommit:
                    state.committed = true;
                    break;
                case LogRecordType::TxnAbort:
                    state.committed = false;
                    break;
                case LogRecordType::TxnEnd:
                    state.ended = true;
                    break;
                default:
                    break;
            }
        }

        if (record.get_type() == LogRecordType::PageWrite ||
            record.get_type() == LogRecordType::CLR) {
            PageImageView image;
            if (!parse_page_image(record, image)) return false;
            const auto page_id = image.page_id.value();
            auto it = m_dirty_page_table.find(page_id);
            if (it == m_dirty_page_table.end()) {
                m_dirty_page_table.emplace(page_id, record.get_lsn());
            } else if (it->second.value() < record.get_lsn().value()) {
                it->second = record.get_lsn();
            }
        }
    }
    return true;
}

bool RecoveryManager::phase_redo() {
    LogReader reader(m_wal_dir);
    if (!reader.initialize()) return !m_redo_start_lsn.is_valid();
    const bool seeked = m_redo_start_lsn.is_valid()
        ? reader.seek_to_lsn(m_redo_start_lsn)
        : reader.seek_to_first();
    if (!seeked) {
        return !m_redo_start_lsn.is_valid() &&
            m_wal_manager->get_flushed_lsn().value() == 0;
    }

    while (auto record_opt = reader.read_next()) {
        const auto& record = *record_opt;
        if (record.get_type() == LogRecordType::PageWrite ||
            record.get_type() == LogRecordType::CLR) {
            if (!redo_record(record)) return false;
        }
    }
    return true;
}

bool RecoveryManager::redo_record(const LogRecord& record) {
    PageImageView image;
    if (!parse_page_image(record, image)) return false;
    if (image.page_size != m_buffer_pool->disk_manager()->page_size()) return false;

    if (image.page_id.value() != 0) {
        auto current_result = m_buffer_pool->fetch_page(image.page_id);
        if (current_result &&
            current_result.value().get().read_header().lsn >= record.get_lsn().value()) {
            return true;
        }
    }

    std::vector<std::byte> page_bytes(image.page_size);
    std::transform(image.after, image.after + image.page_size, page_bytes.begin(),
        [](std::uint8_t byte) { return static_cast<std::byte>(byte); });
    storage::Page page(image.page_id, image.page_size);
    std::copy(page_bytes.begin(), page_bytes.end(), page.data().begin());
    if (image.page_id.value() != 0) {
        auto header = page.read_header();
        header.lsn = record.get_lsn().value();
        header.checksum = 0;
        page.write_header(header);
        header.checksum = storage::calculate_checksum(page.data());
        page.write_header(header);
    }

    auto write_result = m_buffer_pool->write_recovery_page(image.page_id, page.data());
    if (write_result) return true;
    return static_cast<bool>(
        m_buffer_pool->disk_manager()->write_page_from_recovery(page));
}

bool RecoveryManager::phase_undo() {
    struct UndoItem {
        std::uint64_t lsn;
        transaction::TransactionID txn_id;
        bool operator<(const UndoItem& other) const { return lsn < other.lsn; }
    };
    std::priority_queue<UndoItem> undo_queue;

    for (const auto& [txn_id, state] : m_transactions) {
        if (!state.committed && !state.ended && state.last_lsn.is_valid()) {
            undo_queue.push({state.last_lsn.value(), txn_id});
        }
    }

    LogReader reader(m_wal_dir);
    if (!reader.initialize()) return undo_queue.empty();

    while (!undo_queue.empty()) {
        const auto item = undo_queue.top();
        undo_queue.pop();
        if (!reader.seek_to_lsn(LSN(item.lsn))) return false;
        auto record_opt = reader.read_next();
        if (!record_opt || record_opt->get_lsn().value() != item.lsn) return false;

        const auto& record = *record_opt;
        LSN next_lsn = record.get_prev_lsn();
        if (record.get_type() == LogRecordType::PageWrite) {
            if (!undo_record(record)) return false;
        } else if (record.get_type() == LogRecordType::CLR) {
            next_lsn = record.get_undo_next_lsn();
        }

        if (next_lsn.is_valid()) {
            undo_queue.push({next_lsn.value(), item.txn_id});
        }
    }

    for (const auto& [txn_id, state] : m_transactions) {
        if (state.committed || state.ended) continue;
        LogRecord abort_record(txn_id, LogRecordType::TxnAbort, LSN::invalid());
        m_wal_manager->append(abort_record);
        LogRecord end_record(txn_id, LogRecordType::TxnEnd, LSN::invalid());
        m_wal_manager->append(end_record);
    }
    try {
        m_wal_manager->flush();
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool RecoveryManager::undo_record(const LogRecord& record) {
    PageImageView image;
    if (!parse_page_image(record, image)) return false;
    if (image.page_size != m_buffer_pool->disk_manager()->page_size()) return false;

    auto payload = make_page_image_payload(
        image.page_id, image.page_size, image.before, image.before);
    LogRecord clr(record.get_txn_id(), LogRecordType::CLR, LSN::invalid(),
                  record.get_prev_lsn(), std::move(payload));
    LSN clr_lsn;
    try {
        clr_lsn = m_wal_manager->append(clr);
        m_wal_manager->flush_up_to(clr_lsn);
    } catch (const std::exception&) {
        return false;
    }
    return redo_record(clr);
}

} // namespace nemo::wal
