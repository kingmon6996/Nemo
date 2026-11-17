#include "nemo/wal/wal_manager.hpp"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <thread>

namespace nemo::wal {
namespace {

thread_local std::unordered_map<const WALManager*, transaction::TransactionID>
    active_transaction_scopes;

} // namespace

WALManager::TransactionScope::TransactionScope(
    WALManager& manager, transaction::TransactionID txn_id)
    : m_manager(&manager),
      m_previous_id(manager.current_transaction_id()),
      m_had_previous(active_transaction_scopes.contains(&manager)) {
    active_transaction_scopes[&manager] = txn_id;
}

WALManager::TransactionScope::~TransactionScope() {
    if (m_manager == nullptr) return;
    if (m_had_previous) {
        active_transaction_scopes[m_manager] = m_previous_id;
    } else {
        active_transaction_scopes.erase(m_manager);
    }
}

WALManager::TransactionScope::TransactionScope(TransactionScope&& other) noexcept
    : m_manager(other.m_manager),
      m_previous_id(other.m_previous_id),
      m_had_previous(other.m_had_previous) {
    other.m_manager = nullptr;
}

WALManager::TransactionScope WALManager::transaction_scope(
    transaction::TransactionID txn_id) {
    return TransactionScope(*this, txn_id);
}

transaction::TransactionID WALManager::current_transaction_id() const noexcept {
    const auto it = active_transaction_scopes.find(this);
    return it == active_transaction_scopes.end()
        ? transaction::TransactionID(0)
        : it->second;
}

WALManager::WALManager(const std::string& wal_directory, DurabilityMode mode,
                       std::uint64_t max_segment_size)
    : m_wal_directory(wal_directory), m_mode(mode),
      m_max_segment_size(max_segment_size) {
    if (m_max_segment_size < WALSegment::HEADER_SIZE) {
        throw std::invalid_argument("WAL segment size is smaller than its header.");
    }
    if (!std::filesystem::exists(m_wal_directory)) {
        std::filesystem::create_directories(m_wal_directory);
    }

    m_buffer = std::make_unique<LogBuffer>(BUFFER_CAPACITY);
    m_flush_buffer.reserve(BUFFER_CAPACITY);

    std::vector<std::uint64_t> segment_ids;
    for (const auto& entry : std::filesystem::directory_iterator(m_wal_directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".wal") continue;
        const auto stem = entry.path().stem().string();
        std::uint64_t segment_id = 0;
        const auto parsed = std::from_chars(
            stem.data(), stem.data() + stem.size(), segment_id);
        if (parsed.ec != std::errc{} || parsed.ptr != stem.data() + stem.size() ||
            segment_id == 0) {
            throw std::runtime_error("Invalid WAL segment filename.");
        }
        segment_ids.push_back(segment_id);
    }
    std::sort(segment_ids.begin(), segment_ids.end());

    std::uint64_t last_lsn = 0;
    for (const auto segment_id : segment_ids) {
        WALSegment segment(m_wal_directory, segment_id);
        if (!segment.open_read()) {
            throw std::runtime_error("Invalid WAL segment header.");
        }
        std::uint64_t segment_first_lsn = 0;
        std::uint64_t segment_last_lsn = 0;
        for (std::uint64_t offset = WALSegment::HEADER_SIZE;
             offset < segment.get_size();) {
            const auto remaining = segment.get_size() - offset;
            if (remaining < LogRecord::HEADER_SIZE) {
                throw std::runtime_error("Truncated WAL record header.");
            }

            std::vector<std::uint8_t> header(LogRecord::HEADER_SIZE);
            if (!segment.read(offset, header.data(), header.size())) {
                throw std::runtime_error("Failed to read WAL record header.");
            }
            std::uint32_t payload_size = 0;
            std::memcpy(&payload_size, header.data() + 33, sizeof(payload_size));
            const auto record_size =
                static_cast<std::uint64_t>(LogRecord::HEADER_SIZE) + payload_size;
            if (payload_size > BUFFER_CAPACITY - LogRecord::HEADER_SIZE ||
                record_size > remaining) {
                throw std::runtime_error("Invalid or truncated WAL record.");
            }

            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(record_size));
            if (!segment.read(offset, bytes.data(), bytes.size())) {
                throw std::runtime_error("Failed to read WAL record.");
            }
            LogRecord record;
            if (record.deserialize(bytes.data(), bytes.size()) != bytes.size() ||
                !record.verify_checksum() || record.get_lsn().value() == 0 ||
                record.get_lsn().value() <= last_lsn) {
                throw std::runtime_error("Invalid WAL record sequence or checksum.");
            }
            last_lsn = record.get_lsn().value();
            if (segment_first_lsn == 0) {
                segment_first_lsn = last_lsn;
            }
            segment_last_lsn = last_lsn;
            if (record.get_txn_id() != 0) {
                m_transaction_last_lsn[record.get_txn_id()] = record.get_lsn();
                m_max_transaction_id = std::max(
                    m_max_transaction_id, record.get_txn_id());
                if (record.get_type() == LogRecordType::TxnBegin) {
                    m_active_transactions[record.get_txn_id()] = {
                        record.get_txn_id(), record.get_lsn().value(),
                        record.get_lsn().value()};
                } else {
                    auto active = m_active_transactions.find(record.get_txn_id());
                    if (active != m_active_transactions.end()) {
                        active->second.last_lsn = record.get_lsn().value();
                    }
                }
                if (record.get_type() == LogRecordType::TxnCommit ||
                    record.get_type() == LogRecordType::TxnEnd) {
                    m_active_transactions.erase(record.get_txn_id());
                }
            } else if (record.get_type() == LogRecordType::CheckpointEnd) {
                const auto checkpoint =
                    deserialize_checkpoint_state(record.get_payload());
                if (checkpoint) {
                    m_max_transaction_id = std::max(
                        m_max_transaction_id, checkpoint->max_transaction_id);
                    m_active_transactions.clear();
                    m_transaction_last_lsn.clear();
                    for (const auto& txn : checkpoint->transactions) {
                        m_active_transactions[txn.transaction_id] = txn;
                        m_transaction_last_lsn[txn.transaction_id] =
                            LSN(txn.last_lsn);
                    }
                }
            }
            offset += record_size;
        }
        m_segment_lsn_ranges[segment_id] = {
            segment_first_lsn, segment_last_lsn};
        m_wal_size_bytes.fetch_add(segment.get_size(), std::memory_order_relaxed);
    }

    if (last_lsn == std::numeric_limits<std::uint64_t>::max()) {
        throw std::runtime_error("WAL LSN space is exhausted.");
    }
    m_next_lsn.store(last_lsn + 1, std::memory_order_release);
    if (!segment_ids.empty()) m_current_segment_id = segment_ids.back();

    m_current_segment = std::make_unique<WALSegment>(m_wal_directory, m_current_segment_id);
    if (!m_current_segment->open_append(LSN(m_next_lsn.load()))) {
        throw std::runtime_error("Failed to open WAL segment for writing.");
    }
    if (segment_ids.empty()) {
        m_wal_size_bytes.store(
            m_current_segment->get_size(), std::memory_order_release);
    }
    m_segment_lsn_ranges.try_emplace(m_current_segment_id, 0, 0);
    m_flushed_lsn.store(last_lsn, std::memory_order_release);
}

WALManager::~WALManager() {
    try {
        flush();
    } catch (...) {
    }
}

LSN WALManager::append(LogRecord& record) {
    const auto record_size = static_cast<std::size_t>(record.get_size());
    if (record_size > m_buffer->capacity()) {
        throw std::length_error("Log record exceeds WAL buffer capacity.");
    }

    for (;;) {
        std::unique_lock<std::mutex> append_lock(m_append_mutex);
        if (m_failed.load(std::memory_order_acquire)) {
            throw std::runtime_error("WAL manager is in a failed state.");
        }

        if (m_buffer->capacity() - m_buffer->size() < record_size) {
            append_lock.unlock();
            flush();
            continue;
        }

        return append_locked(record);
    }
}

LSN WALManager::append_locked(LogRecord& record) {
    const auto assigned_value = m_next_lsn.load(std::memory_order_relaxed);
    if (assigned_value == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("WAL LSN space is exhausted.");
    }
    const LSN assigned_lsn(assigned_value);
    const auto txn_id = record.get_txn_id();
    if (txn_id != 0) {
        const auto previous = m_transaction_last_lsn.find(txn_id);
        if (previous != m_transaction_last_lsn.end()) {
            record.set_prev_lsn(previous->second);
        }
    }
    record.set_lsn(assigned_lsn);
    if (!m_buffer->append(record)) {
        throw std::logic_error("WAL buffer capacity changed during append.");
    }

    if (m_buffer_last_lsn == 0) {
        m_buffer_first_lsn = assigned_value;
        m_first_buffered_at = std::chrono::steady_clock::now();
    }
    m_buffer_last_lsn = assigned_value;
    m_next_lsn.store(assigned_value + 1, std::memory_order_release);
    if (txn_id != 0) {
        m_transaction_last_lsn[txn_id] = assigned_lsn;
        m_max_transaction_id = std::max(m_max_transaction_id, txn_id);
        if (record.get_type() == LogRecordType::TxnBegin) {
            m_active_transactions[txn_id] = {
                txn_id, assigned_value, assigned_value};
        } else {
            auto active = m_active_transactions.find(txn_id);
            if (active != m_active_transactions.end()) {
                active->second.last_lsn = assigned_value;
            }
        }
        if (record.get_type() == LogRecordType::TxnCommit ||
            record.get_type() == LogRecordType::TxnEnd) {
            m_active_transactions.erase(txn_id);
        }
    }
    return assigned_lsn;
}

LSN WALManager::append_checkpoint_end(
    std::vector<CheckpointDirtyPage> dirty_pages) {
    for (;;) {
        std::unique_lock<std::mutex> append_lock(m_append_mutex);
        if (m_failed.load(std::memory_order_acquire)) {
            throw std::runtime_error("WAL manager is in a failed state.");
        }

        CheckpointState state;
        state.max_transaction_id = m_max_transaction_id;
        state.dirty_pages = dirty_pages;
        state.transactions.reserve(m_active_transactions.size());
        for (const auto& [_, transaction] : m_active_transactions) {
            state.transactions.push_back(transaction);
        }
        LogRecord record(
            0, LogRecordType::CheckpointEnd, LSN::invalid(),
            serialize_checkpoint_state(state));
        if (record.get_size() > m_buffer->capacity()) {
            throw std::length_error(
                "Checkpoint state exceeds WAL buffer capacity.");
        }
        if (m_buffer->capacity() - m_buffer->size() < record.get_size()) {
            append_lock.unlock();
            flush();
            continue;
        }
        return append_locked(record);
    }
}

std::vector<CheckpointTransaction> WALManager::active_transactions() {
    std::lock_guard<std::mutex> lock(m_append_mutex);
    std::vector<CheckpointTransaction> transactions;
    transactions.reserve(m_active_transactions.size());
    for (const auto& [_, transaction] : m_active_transactions) {
        transactions.push_back(transaction);
    }
    return transactions;
}

void WALManager::roll_segment_if_needed() {
    const auto current_size = m_current_segment->get_size();
    if (current_size + m_flush_buffer.size() <= m_max_segment_size) {
        return;
    }

    m_current_segment->close();
    if (m_current_segment_id == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("WAL segment ID space is exhausted.");
    }
    ++m_current_segment_id;
    m_current_segment = std::make_unique<WALSegment>(
        m_wal_directory, m_current_segment_id);
    if (!m_current_segment->open_append(LSN(m_flush_first_lsn))) {
        throw std::runtime_error("Failed to roll WAL segment.");
    }
    m_segment_lsn_ranges.try_emplace(m_current_segment_id, 0, 0);
    m_wal_size_bytes.fetch_add(
        WALSegment::HEADER_SIZE, std::memory_order_relaxed);
}

bool WALManager::should_wait_for_group_commit(
    std::size_t buffered_bytes) noexcept {
    return buffered_bytes < GROUP_COMMIT_SIZE;
}

void WALManager::execute_flush(bool wait_for_batch) {
    std::unique_lock<std::mutex> flush_lock(m_flush_mutex);
    if (m_failed.load(std::memory_order_acquire)) {
        throw std::runtime_error("WAL manager is in a failed state.");
    }

    if (wait_for_batch && m_mode == DurabilityMode::Normal) {
        std::chrono::steady_clock::time_point deadline;
        bool should_wait = false;
        {
            std::lock_guard<std::mutex> append_lock(m_append_mutex);
            if (m_buffer_last_lsn != 0 &&
                should_wait_for_group_commit(m_buffer->size())) {
                deadline = m_first_buffered_at + GROUP_COMMIT_WINDOW;
                should_wait = std::chrono::steady_clock::now() < deadline;
            }
        }
        if (should_wait) {
            std::this_thread::sleep_until(deadline);
        }
    }

    {
        std::lock_guard<std::mutex> append_lock(m_append_mutex);
        if (m_buffer_last_lsn == 0) return;
        m_flush_first_lsn = m_buffer_first_lsn;
        m_flush_last_lsn = m_buffer_last_lsn;
        m_buffer_first_lsn = 0;
        m_buffer_last_lsn = 0;
        m_first_buffered_at = {};
        m_buffer->swap(m_flush_buffer);
    }

    try {
        roll_segment_if_needed();
        if (m_before_segment_append_for_testing) {
            m_before_segment_append_for_testing();
        }
        if (!m_current_segment->append(
                m_flush_buffer.data(), m_flush_buffer.size())) {
            throw std::runtime_error("Failed to write to WAL segment.");
        }
        if (m_before_segment_flush_for_testing) {
            m_before_segment_flush_for_testing();
        }
        m_current_segment->flush();
        auto& range = m_segment_lsn_ranges[m_current_segment_id];
        if (range.first == 0) range.first = m_flush_first_lsn;
        range.second = m_flush_last_lsn;
        m_wal_size_bytes.fetch_add(
            m_flush_buffer.size(), std::memory_order_relaxed);
        m_flushed_lsn.store(m_flush_last_lsn, std::memory_order_release);
        m_flush_first_lsn = 0;
        m_flush_last_lsn = 0;
        m_flush_buffer.clear();
    } catch (...) {
        m_failed.store(true, std::memory_order_release);
        throw;
    }
}

void WALManager::flush() {
    execute_flush(false);
}

void WALManager::truncate_before(LSN first_required_lsn) {
    flush();
    const auto cutoff = first_required_lsn.value();
    std::lock_guard<std::mutex> flush_lock(m_flush_mutex);
    std::lock_guard<std::mutex> append_lock(m_append_mutex);
    if (m_failed.load(std::memory_order_acquire)) {
        throw std::runtime_error("WAL manager is in a failed state.");
    }

    for (auto it = m_segment_lsn_ranges.begin();
         it != m_segment_lsn_ranges.end();) {
        if (it->first == m_current_segment_id ||
            (it->second.second != 0 && it->second.second >= cutoff)) {
            ++it;
            continue;
        }

        WALSegment segment(m_wal_directory, it->first);
        std::error_code size_error;
        const auto size = std::filesystem::file_size(segment.get_path(), size_error);
        if (size_error) {
            throw std::runtime_error("Failed to inspect an old WAL segment.");
        }
        std::error_code remove_error;
        if (!std::filesystem::remove(segment.get_path(), remove_error) ||
            remove_error) {
            throw std::runtime_error("Failed to remove a checkpointed WAL segment.");
        }
        m_wal_size_bytes.fetch_sub(size, std::memory_order_relaxed);
        it = m_segment_lsn_ranges.erase(it);
    }
}

void WALManager::flush_up_to(LSN target_lsn) {
    const auto target_value = target_lsn.value();
    if (target_value == 0) return;

    if (target_value >= m_next_lsn.load(std::memory_order_acquire)) {
        throw std::invalid_argument("Cannot flush an LSN that has not been appended.");
    }

    while (m_flushed_lsn.load(std::memory_order_acquire) < target_value) {
        execute_flush(true);
    }
}

} // namespace nemo::wal
