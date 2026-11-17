#include "nemo/wal/checkpoint_manager.hpp"

#include "nemo/storage/checksum.hpp"
#include "nemo/logging/logger.hpp"
#include "nemo/wal/log_reader.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef ERROR
#undef ERROR
#endif
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace nemo::wal {
namespace {

constexpr std::array<std::uint8_t, 8> MASTER_MAGIC{
    'N', 'E', 'M', 'O', 'M', 'C', 'P', '1'};
constexpr std::uint32_t MASTER_VERSION = 1;
constexpr std::size_t MASTER_RECORD_SIZE = 32;
constexpr std::uint64_t WAL_GROWTH_CHECKPOINT_BYTES =
    64ULL * 1024ULL * 1024ULL;
constexpr auto CHECKPOINT_INTERVAL = std::chrono::minutes(5);

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

std::uint32_t master_checksum(const std::vector<std::uint8_t>& bytes) {
    std::vector<std::byte> checksum_bytes(bytes.size());
    std::transform(bytes.begin(), bytes.end(), checksum_bytes.begin(),
        [](std::uint8_t byte) { return static_cast<std::byte>(byte); });
    return storage::calculate_checksum(checksum_bytes);
}

bool durably_flush_file(const std::filesystem::path& path) {
#ifdef _WIN32
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const BOOL flushed = FlushFileBuffers(file);
    CloseHandle(file);
    return flushed != 0;
#else
    const int file = ::open(path.c_str(), O_RDONLY);
    if (file < 0) return false;
    const int result = ::fsync(file);
    const int saved_errno = errno;
    ::close(file);
    errno = saved_errno;
    return result == 0;
#endif
}

bool replace_master_file(const std::filesystem::path& source,
                        const std::filesystem::path& destination) {
#ifdef _WIN32
    return MoveFileExW(
        source.c_str(), destination.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    if (error) return false;
    const auto parent = destination.parent_path().empty()
        ? std::filesystem::path(".")
        : destination.parent_path();
    const int directory = ::open(parent.c_str(), O_RDONLY);
    if (directory < 0) return false;
    const int result = ::fsync(directory);
    const int saved_errno = errno;
    ::close(directory);
    errno = saved_errno;
    return result == 0;
#endif
}

} // namespace

CheckpointManager::CheckpointManager(
    WALManager* wal, transaction::TransactionManager* txn_mgr,
    storage::BufferPool* buffer_pool,
    const std::string& master_record_path, const std::string& wal_dir)
    : m_wal_manager(wal),
      m_txn_manager(txn_mgr),
      m_buffer_pool(buffer_pool),
      m_master_record_path(master_record_path),
      m_wal_directory(wal_dir),
      m_last_checkpoint_at(std::chrono::steady_clock::now()) {}

bool CheckpointManager::perform_checkpoint() {
    std::lock_guard<std::mutex> checkpoint_lock(m_checkpoint_mutex);
    if (m_wal_manager == nullptr || m_buffer_pool == nullptr) return false;

    try {
        auto checkpoint_guard =
            m_buffer_pool->disk_manager()->checkpoint_guard();
        auto flush_result = m_buffer_pool->flush_all();
        if (!flush_result) return false;
        auto disk_flush_result = m_buffer_pool->disk_manager()->flush();
        if (!disk_flush_result) return false;
        m_wal_manager->flush();

        LogRecord begin_record(
            0, LogRecordType::CheckpointBegin, LSN::invalid());
        const auto begin_lsn = m_wal_manager->append(begin_record);
        const auto end_lsn = m_wal_manager->append_checkpoint_end({});
        m_wal_manager->flush_up_to(end_lsn);

        const auto state = read_checkpoint_state(begin_lsn);
        if (!state) return false;

        CheckpointMetadata metadata{
            begin_lsn,
            static_cast<std::uint64_t>(
                std::chrono::system_clock::now().time_since_epoch().count())};
        if (!write_master_record(metadata)) return false;

        auto first_required_lsn = begin_lsn.value();
        for (const auto& txn : state->transactions) {
            first_required_lsn =
                (std::min)(first_required_lsn, txn.first_lsn);
        }
        for (const auto& page : state->dirty_pages) {
            first_required_lsn =
                (std::min)(first_required_lsn, page.rec_lsn);
        }
        m_wal_manager->truncate_before(LSN(first_required_lsn));
        m_last_checkpoint_at = std::chrono::steady_clock::now();
        m_last_checkpoint_wal_bytes = m_wal_manager->wal_size_bytes();
        return true;
    } catch (const std::exception& error) {
        NEMO_LOG_ERROR("Checkpoint", error.what());
        return false;
    }
}

bool CheckpointManager::write_master_record(
    const CheckpointMetadata& metadata) {
    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), MASTER_MAGIC.begin(), MASTER_MAGIC.end());
    append_u32(bytes, MASTER_VERSION);
    append_u64(bytes, metadata.checkpoint_lsn.value());
    append_u64(bytes, metadata.timestamp);
    append_u32(bytes, master_checksum(bytes));

    const std::filesystem::path master_path(m_master_record_path);
    const auto temporary_path = master_path.string() + ".tmp";
    {
        std::ofstream output(
            temporary_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) return false;
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) return false;
    }
    if (!durably_flush_file(temporary_path)) return false;
    if (!replace_master_file(temporary_path, master_path)) return false;
    return true;
}

std::optional<CheckpointMetadata> CheckpointManager::read_master_record() {
    std::ifstream input(m_master_record_path, std::ios::binary);
    if (!input.is_open()) return std::nullopt;
    std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());

    if (bytes.size() == sizeof(std::uint64_t) * 2) {
        const CheckpointMetadata legacy{
            LSN(read_u64(bytes.data())),
            read_u64(bytes.data() + sizeof(std::uint64_t)),
            true};
        return legacy.checkpoint_lsn.value() == 0
            ? std::nullopt
            : std::optional<CheckpointMetadata>(legacy);
    }
    if (bytes.size() != MASTER_RECORD_SIZE ||
        !std::equal(MASTER_MAGIC.begin(), MASTER_MAGIC.end(), bytes.begin()) ||
        read_u32(bytes.data() + 8) != MASTER_VERSION ||
        read_u32(bytes.data() + 28) != master_checksum(
            std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + 28))) {
        return std::nullopt;
    }

    CheckpointMetadata metadata{
        LSN(read_u64(bytes.data() + 12)),
        read_u64(bytes.data() + 20)};
    if (!metadata.checkpoint_lsn.is_valid()) return std::nullopt;
    return metadata;
}

std::optional<CheckpointState> CheckpointManager::read_checkpoint_state(
    LSN begin_lsn) const {
    LogReader reader(m_wal_directory);
    if (!reader.initialize() || !reader.seek_to_lsn(begin_lsn)) {
        return std::nullopt;
    }
    auto begin = reader.read_next();
    if (!begin || begin->get_lsn().value() != begin_lsn.value() ||
        begin->get_type() != LogRecordType::CheckpointBegin) {
        return std::nullopt;
    }
    while (auto record = reader.read_next()) {
        if (record->get_type() == LogRecordType::CheckpointEnd) {
            return deserialize_checkpoint_state(record->get_payload());
        }
    }
    return std::nullopt;
}

bool CheckpointManager::has_master_record() const {
    std::error_code error;
    const bool exists = std::filesystem::exists(m_master_record_path, error);
    return exists || static_cast<bool>(error);
}

bool CheckpointManager::checkpoint_due() const {
    std::lock_guard<std::mutex> checkpoint_lock(m_checkpoint_mutex);
    if (m_wal_manager == nullptr) return false;
    const auto wal_bytes = m_wal_manager->wal_size_bytes();
    const auto wal_growth = wal_bytes >= m_last_checkpoint_wal_bytes
        ? wal_bytes - m_last_checkpoint_wal_bytes
        : 0;
    return wal_growth >= WAL_GROWTH_CHECKPOINT_BYTES ||
         std::chrono::steady_clock::now() - m_last_checkpoint_at >=
             CHECKPOINT_INTERVAL;
}

} // namespace nemo::wal
