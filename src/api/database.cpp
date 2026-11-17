#include "nemo/api/database.hpp"
#include "nemo/api/connection.hpp"
#include "nemo/runtime/connection_manager.hpp"
#include "nemo/runtime/connection_pool.hpp"
#include "nemo/runtime/connection_lease.hpp"
#include "nemo/runtime/worker_pool.hpp"
#include "nemo/runtime/runtime_metrics.hpp"
#include "nemo/storage/disk_manager.hpp"
#include "nemo/storage/buffer_pool.hpp"
#include "nemo/storage/database_header.hpp"   // Phase 1: DatabaseHeader::SIZE
#include "nemo/catalog/catalog.hpp"
#include "nemo/core/constants.hpp"
#include "nemo/wal/wal_manager.hpp"
#include "nemo/wal/checkpoint_manager.hpp"   // Phase 4
#include "nemo/wal/recovery_manager.hpp"     // Phase 4
#include "nemo/transaction/transaction_manager.hpp" // Phase 4
#include "nemo/logging/logger.hpp"

#include "api_internal.hpp"

#include <filesystem>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <limits>
#include <mutex>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#  include <windows.h>
#endif

// ---------------------------------------------------------------------------
// Platform RAM detection (Phase 2)
// ---------------------------------------------------------------------------
#if defined(_WIN32)
#  include <windows.h>
#elif defined(__APPLE__)
#  include <sys/types.h>
#  include <sys/sysctl.h>
#elif defined(__linux__)
#  include <sys/sysinfo.h>
#endif

#ifdef ERROR
#  undef ERROR
#endif

namespace {

/// Returns bytes of RAM currently available to the OS (not total installed RAM).
/// Falls back to DEFAULT_BUFFER_POOL_SIZE * 4 if the query fails.
std::size_t query_available_ram_bytes() {
#if defined(_WIN32)
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        return static_cast<std::size_t>(ms.ullAvailPhys);
    }
#elif defined(__APPLE__)
    // vm.page_free_count * hw.pagesize gives available pages
    std::uint32_t page_size = 0;
    std::size_t psz = sizeof(page_size);
    sysctlbyname("hw.pagesize", &page_size, &psz, nullptr, 0);
    std::uint32_t free_pages = 0;
    std::size_t fpz = sizeof(free_pages);
    sysctlbyname("vm.page_free_count", &free_pages, &fpz, nullptr, 0);
    if (page_size > 0 && free_pages > 0) {
        return static_cast<std::size_t>(page_size) * static_cast<std::size_t>(free_pages);
    }
#elif defined(__linux__)
    struct sysinfo si{};
    if (sysinfo(&si) == 0) {
        return static_cast<std::size_t>(si.freeram) * static_cast<std::size_t>(si.mem_unit);
    }
#endif
    // Fallback: return 4× the compile-time default
    return nemo::core::DEFAULT_BUFFER_POOL_SIZE * 4;
}

/// Compute the pool size from a byte override or one quarter of available RAM.
std::size_t compute_buffer_pool_bytes(std::size_t requested_bytes) {
    const auto available_ram_bytes = requested_bytes == 0
        ? query_available_ram_bytes()
        : 0;
    return nemo::core::clamp_buffer_pool_bytes(requested_bytes, available_ram_bytes);
}

/// Convert byte size to frame count (page slots in the buffer pool).
std::size_t bytes_to_frame_count(std::size_t pool_bytes, std::size_t page_size) {
    std::size_t frames = pool_bytes / page_size;
    return (frames < 32) ? 32 : frames;   // minimum 32 frames
}

bool has_file_magic(const std::filesystem::path& path, const char* magic) {
    std::ifstream input(path, std::ios::binary);
    char actual[8]{};
    input.read(actual, sizeof(actual));
    return input.gcount() == static_cast<std::streamsize>(sizeof(actual)) &&
        std::equal(std::begin(actual), std::end(actual), magic);
}

std::filesystem::path legacy_backup_path(const std::filesystem::path& data_path) {
    auto candidate = std::filesystem::path(data_path.string() + ".legacy.bak");
    for (std::uint32_t suffix = 1; std::filesystem::exists(candidate); ++suffix) {
        candidate = std::filesystem::path(
            data_path.string() + ".legacy.bak." + std::to_string(suffix));
    }
    return candidate;
}

bool replace_file_atomically(const std::filesystem::path& source,
                             const std::filesystem::path& destination) {
#if defined(_WIN32)
    return MoveFileExW(source.c_str(), destination.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    return !error;
#endif
}

struct StoragePointerGuard {
    nemo::api::SharedDatabaseState& state;
    ~StoragePointerGuard() {
        state.buffer_pool = nullptr;
        state.disk_manager = nullptr;
    }
};

struct StagingFileGuard {
    std::filesystem::path path;
    bool owns_file{false};

    ~StagingFileGuard() {
        if (!owns_file) return;
        std::error_code error;
        std::filesystem::remove(path, error);
        if (error) {
            nemo::logging::Logger::get_instance().log(
                nemo::logging::LogLevel::INFO,
                "Storage",
                "Failed to remove incomplete migration staging file");
        }
    }
};

} // anonymous namespace

namespace nemo {

class DatabaseImpl {
public:
    DatabaseConfig config;
    std::shared_ptr<api::SharedDatabaseState> shared_state;
    std::unique_ptr<storage::DiskManager>            disk_manager;
    std::unique_ptr<wal::WALManager>                 wal_manager;         // Phase 3
    std::unique_ptr<storage::BufferPool>             buffer_pool;
    std::unique_ptr<transaction::TransactionManager> txn_manager;         // Phase 4
    std::unique_ptr<wal::CheckpointManager>          checkpoint_manager;  // Phase 4
    std::unique_ptr<wal::RecoveryManager>            recovery_manager;    // Phase 4
    std::unique_ptr<catalog::Catalog>                catalog;
    bool recovery_complete{false};                                         // Phase 4
    bool state_ready{false};

    runtime::RuntimeMetrics metrics;
    std::shared_ptr<runtime::ConnectionManager> conn_manager;
    std::shared_ptr<runtime::ConnectionPool>    conn_pool;
    std::shared_ptr<runtime::WorkerPool>        worker_pool;
    std::mutex checkpoint_worker_mutex;
    std::condition_variable checkpoint_worker_cv;
    std::thread checkpoint_worker;
    bool stop_checkpoint_worker{false};
};

Database::Database() : m_impl(std::make_unique<DatabaseImpl>()) {}
Database::~Database() { close(); }
Database::Database(Database&&) noexcept = default;
Database& Database::operator=(Database&&) noexcept = default;

Result<Database> Database::open(std::string_view path, const DatabaseConfig& config) {
    Database db;
    db.m_impl->config = config;
    db.m_impl->shared_state = std::make_shared<api::SharedDatabaseState>();
    db.m_impl->shared_state->db_path = std::string(path);

    // -----------------------------------------------------------------------
    // Phase 2: Instantiate DiskManager + BufferPool when a path is provided.
    // The path points to .nemo/<name>/data.nm (resolved by the Python layer).
    // -----------------------------------------------------------------------
    if (!path.empty()) {
        namespace fs = std::filesystem;
        const fs::path data_path(path);

        // Compute pool parameters
        // config.buffer_pool_size == 0 → auto-detect (1/4 available RAM).
        // A nonzero value is an exact byte request before clamping.
        std::size_t pool_bytes = compute_buffer_pool_bytes(config.buffer_pool_size);
        std::size_t page_size = core::DEFAULT_PAGE_SIZE;
        std::size_t frame_count = bytes_to_frame_count(pool_bytes, page_size);

        // Build a storage::Config for DiskManager
        Config storage_cfg;
        storage_cfg.set_page_size(page_size);
        storage_cfg.set_buffer_pool_size(frame_count);
        storage_cfg.set_max_connections(config.connection_pool_size);

        bool is_existing_db = false;
        const bool data_exists = fs::exists(data_path);
        const auto data_size = data_exists ? fs::file_size(data_path) : 0;
        const bool is_legacy =
            data_size >= 8 && has_file_magic(data_path, "NEMOMETA");
        const bool is_page_database =
            data_size >= storage::DatabaseHeader::SIZE &&
            has_file_magic(data_path, storage::MAGIC_BYTES.data());

        if (data_size != 0 && !is_legacy && !is_page_database) {
            return Status::Corruption;
        }

        is_existing_db = is_page_database;
        if (is_legacy) {
            if (!db.m_impl->shared_state->load_legacy_state()) {
                return Status::Corruption;
            }
            StoragePointerGuard pointer_guard{*db.m_impl->shared_state};

            const fs::path staging_path(data_path.string() + ".phase1.tmp");
            if (fs::exists(staging_path)) {
                return Status::AlreadyExists;
            }
            StagingFileGuard staging_guard{staging_path};
            staging_guard.owns_file = true;

            auto staging_disk_result = storage::DiskManager::create(staging_path, storage_cfg);
            if (!staging_disk_result) return staging_disk_result.error();
            auto staging_disk = std::move(staging_disk_result.value());
            auto staging_pool = std::make_unique<storage::BufferPool>(
                frame_count, staging_disk.get(), nullptr);
            db.m_impl->shared_state->disk_manager = staging_disk.get();
            db.m_impl->shared_state->buffer_pool = staging_pool.get();

            auto persist_result = db.m_impl->shared_state->persist_state();
            if (!persist_result) return persist_result.error();
            auto flush_result = staging_pool->flush_all();
            if (!flush_result) return flush_result.error();
            flush_result = staging_disk->flush();
            if (!flush_result) return flush_result.error();
            db.m_impl->shared_state->buffer_pool = nullptr;
            db.m_impl->shared_state->disk_manager = nullptr;
            staging_pool.reset();
            auto close_result = staging_disk->close();
            if (!close_result) return close_result.error();
            staging_disk.reset();

            auto validation_disk_result = storage::DiskManager::open(staging_path, storage_cfg);
            if (!validation_disk_result) return validation_disk_result.error();
            auto validation_disk = std::move(validation_disk_result.value());
            auto validation_pool = std::make_unique<storage::BufferPool>(
                frame_count, validation_disk.get(), nullptr);
            api::SharedDatabaseState validation_state;
            validation_state.db_path = staging_path.string();
            validation_state.disk_manager = validation_disk.get();
            validation_state.buffer_pool = validation_pool.get();
            auto validation_result = validation_state.load_page_state();
            if (!validation_result) return validation_result.error();
            {
                std::shared_lock expected_db_lock(db.m_impl->shared_state->db_mutex);
                std::shared_lock actual_db_lock(validation_state.db_mutex);
                if (db.m_impl->shared_state->tables.size() != validation_state.tables.size() ||
                    db.m_impl->shared_state->sequences.size() != validation_state.sequences.size()) {
                    return Status::Corruption;
                }
                for (const auto& [name, expected_table] : db.m_impl->shared_state->tables) {
                    const auto actual = validation_state.tables.find(name);
                    if (actual == validation_state.tables.end() ||
                        expected_table->columns.size() != actual->second->columns.size() ||
                        expected_table->sample_rows.size() != actual->second->sample_rows.size()) {
                        return Status::Corruption;
                    }
                    std::shared_lock expected_table_lock(expected_table->mutex);
                    std::shared_lock actual_table_lock(actual->second->mutex);
                    for (std::size_t column = 0; column < expected_table->columns.size(); ++column) {
                        const auto& expected = expected_table->columns[column];
                        const auto& migrated = actual->second->columns[column];
                        if (expected.name != migrated.name || expected.type != migrated.type ||
                            expected.nullable != migrated.nullable ||
                            expected.is_autoincrement != migrated.is_autoincrement ||
                            expected.sequence_name != migrated.sequence_name) {
                            return Status::Corruption;
                        }
                    }
                    for (std::size_t row = 0; row < expected_table->sample_rows.size(); ++row) {
                        const auto& expected = expected_table->sample_rows[row];
                        const auto& migrated = actual->second->sample_rows[row];
                        if (expected.size() != migrated.size()) return Status::Corruption;
                        for (std::size_t column = 0; column < expected.size(); ++column) {
                            if (!(expected[column] == migrated[column])) return Status::Corruption;
                        }
                    }
                }
                for (const auto& [name, expected_sequence] : db.m_impl->shared_state->sequences) {
                    const auto actual = validation_state.sequences.find(name);
                    if (actual == validation_state.sequences.end()) return Status::Corruption;
                    std::lock_guard expected_lock(expected_sequence->mutex);
                    std::lock_guard actual_lock(actual->second->mutex);
                    if (expected_sequence->name != actual->second->name ||
                        expected_sequence->current_val != actual->second->current_val ||
                        expected_sequence->start_with != actual->second->start_with ||
                        expected_sequence->increment_by != actual->second->increment_by ||
                        expected_sequence->is_called != actual->second->is_called) {
                        return Status::Corruption;
                    }
                }
            }
            validation_state.buffer_pool = nullptr;
            validation_state.disk_manager = nullptr;
            validation_pool.reset();
            close_result = validation_disk->close();
            if (!close_result) return close_result.error();
            validation_disk.reset();

            std::error_code copy_error;
            fs::copy_file(data_path, legacy_backup_path(data_path),
                          fs::copy_options::none, copy_error);
            if (copy_error) return Status::IOError;
            if (!replace_file_atomically(staging_path, data_path)) {
                return Status::IOError;
            }
            staging_guard.owns_file = false;
            is_existing_db = true;
        }

        if (data_exists && data_size == 0) {
            std::error_code remove_error;
            fs::remove(data_path, remove_error);
            if (remove_error) return Status::IOError;
        }

        auto disk_result = is_existing_db
            ? storage::DiskManager::open(data_path, storage_cfg)
            : storage::DiskManager::create(data_path, storage_cfg);
        if (!disk_result) return disk_result.error();
        db.m_impl->disk_manager = std::move(disk_result.value());

        // Do not open the database without its WAL: page writes depend on it.
        fs::path wal_path;
        try {
            wal_path = data_path.filename() == "data.nm"
                ? data_path.parent_path() / "wal"
                : fs::path(data_path.string() + ".wal");
            std::string wal_dir = wal_path.string();
            db.m_impl->wal_manager = std::make_unique<wal::WALManager>(
                wal_dir, wal::DurabilityMode::Normal);
        } catch (const std::exception& error) {
            NEMO_LOG_ERROR("WAL", error.what());
            return Status::IOError;
        }
        if (db.m_impl->wal_manager->max_transaction_id() ==
            (std::numeric_limits<transaction::TransactionID>::max)()) {
            return Status::Corruption;
        }
        db.m_impl->shared_state->next_txn_id.store(
            db.m_impl->wal_manager->max_transaction_id() + 1,
            std::memory_order_relaxed);
        db.m_impl->disk_manager->set_wal_manager(db.m_impl->wal_manager.get());

        db.m_impl->buffer_pool = std::make_unique<storage::BufferPool>(
            frame_count,
            db.m_impl->disk_manager.get(),
            db.m_impl->wal_manager.get());
        db.m_impl->shared_state->disk_manager = db.m_impl->disk_manager.get();
        db.m_impl->shared_state->buffer_pool = db.m_impl->buffer_pool.get();
        db.m_impl->shared_state->wal_manager = db.m_impl->wal_manager.get();

        // Phase 3: inject WAL manager into SharedDatabaseState so the connection
        // layer can append log records for every write.

        // -----------------------------------------------------------------------
        // Phase 4: Construct TransactionManager, CheckpointManager, RecoveryManager
        // Recovery must finish before page-backed SQL state is loaded.
        // -----------------------------------------------------------------------
        try {
            // TransactionManager: LockManager is nullptr until Phase 5
            db.m_impl->txn_manager =
                std::make_unique<transaction::TransactionManager>(
                    nullptr,                          // lock_manager (Phase 5)
                    db.m_impl->wal_manager.get());    // wal_manager

            const auto master_record_path = data_path.filename() == "data.nm"
                ? data_path.parent_path() / "master.nm"
                : fs::path(data_path.string() + ".master.nm");
            db.m_impl->checkpoint_manager =
                std::make_unique<wal::CheckpointManager>(
                    db.m_impl->wal_manager.get(),
                    db.m_impl->txn_manager.get(),
                    db.m_impl->buffer_pool.get(),
                    master_record_path.string(),
                    wal_path.string());

            // RecoveryManager: ARIES over .nemo/<name>/wal/
            if (db.m_impl->buffer_pool) {
                db.m_impl->recovery_manager =
                    std::make_unique<wal::RecoveryManager>(
                        db.m_impl->wal_manager.get(),
                        db.m_impl->checkpoint_manager.get(),
                        db.m_impl->buffer_pool.get(),
                        wal_path.string());

                if (is_existing_db) {
                    db.m_impl->recovery_complete =
                        db.m_impl->recovery_manager->recover();
                    if (!db.m_impl->recovery_complete) return Status::IOError;
                } else {
                    // Fresh database: nothing to recover.
                    db.m_impl->recovery_complete = true;
                }
            } else {
                // No buffer pool — in-memory/degraded mode.
                // For a fresh DB there is nothing to recover; for existing DBs
                // we cannot replay WAL (degraded), but still mark complete on fresh.
                db.m_impl->recovery_complete = !is_existing_db;
            }
        } catch (const std::exception& error) {
            NEMO_LOG_ERROR("Recovery", error.what());
            return Status::IOError;
        }

        if (is_existing_db) {
            auto load_result = db.m_impl->shared_state->load_page_state();
            if (!load_result) return load_result.error();
        }
        db.m_impl->state_ready = true;
    }

    db.m_impl->conn_manager = std::make_shared<runtime::ConnectionManager>();
    db.m_impl->conn_pool    = std::make_shared<runtime::ConnectionPool>(
        db.m_impl->conn_manager, config.connection_pool_size);
    db.m_impl->worker_pool  = std::make_shared<runtime::WorkerPool>(
        config.worker_count, 1024, db.m_impl->metrics);
    db.m_impl->worker_pool->start();
    if (db.m_impl->checkpoint_manager && db.m_impl->state_ready) {
        auto* impl = db.m_impl.get();
        db.m_impl->checkpoint_worker = std::thread([impl] {
            std::unique_lock<std::mutex> lock(impl->checkpoint_worker_mutex);
            while (!impl->checkpoint_worker_cv.wait_for(
                lock, std::chrono::seconds(30),
                [impl] { return impl->stop_checkpoint_worker; })) {
                lock.unlock();
                if (impl->checkpoint_manager->checkpoint_due() &&
                    !impl->checkpoint_manager->perform_checkpoint()) {
                    NEMO_LOG_ERROR(
                        "Checkpoint", "Automatic checkpoint failed.");
                }
                lock.lock();
            }
        });
    }

    return db;
}

Result<Database> Database::create(std::string_view path, const DatabaseConfig& config) {
    return open(path, config);
}

Result<api::Connection> Database::connect(const api::ConnectionConfig& config) {
    if (!m_impl || !m_impl->conn_pool) return Status::InvalidState;

    auto lease = m_impl->conn_pool->acquire();
    if (!lease) {
        return Status::Timeout;
    }

    lease->get()->session().read_only = config.read_only;
    return api::create_connection(std::move(lease), m_impl->worker_pool, m_impl->shared_state);
}

Result<void> Database::close() {
    if (m_impl) {
        Status close_status = Status::OK;
        if (m_impl->checkpoint_worker.joinable()) {
            {
                std::lock_guard<std::mutex> lock(
                    m_impl->checkpoint_worker_mutex);
                m_impl->stop_checkpoint_worker = true;
            }
            m_impl->checkpoint_worker_cv.notify_all();
            m_impl->checkpoint_worker.join();
        }
        // Flush the WAL before persist_state can flush dirty data pages.
        if (m_impl->wal_manager) {
            try {
                m_impl->wal_manager->flush();
            } catch (const std::exception& error) {
                NEMO_LOG_ERROR("WAL", error.what());
                if (close_status == Status::OK) close_status = Status::IOError;
            }
        }
        if (close_status == Status::OK && m_impl->state_ready &&
            m_impl->shared_state) {
            auto persist_result = m_impl->shared_state->persist_state();
            if (!persist_result) close_status = persist_result.error();
        }
        // Phase 2: flush all dirty buffer-pool pages to disk before close
        if (m_impl->buffer_pool) {
            if (m_impl->state_ready) {
                auto flush_result = m_impl->buffer_pool->flush_all();
                if (!flush_result && close_status == Status::OK) {
                    close_status = flush_result.error();
                }
                if (close_status == Status::OK &&
                    m_impl->checkpoint_manager &&
                    !m_impl->checkpoint_manager->perform_checkpoint()) {
                    close_status = Status::IOError;
                }
            } else {
                m_impl->buffer_pool->discard_dirty_pages();
            }
            m_impl->buffer_pool.reset();
        }
        if (m_impl->shared_state) {
            m_impl->shared_state->buffer_pool = nullptr;
            m_impl->shared_state->disk_manager = nullptr;
        }
        if (m_impl->disk_manager) {
            auto disk_close_result = m_impl->state_ready
                ? m_impl->disk_manager->close()
                : m_impl->disk_manager->close_without_flush();
            if (!disk_close_result && close_status == Status::OK) {
                close_status = disk_close_result.error();
            }
        }
        if (m_impl->worker_pool) {
            m_impl->worker_pool->shutdown();
            m_impl->conn_pool->shutdown();
            m_impl->conn_manager->shutdown();
        }
        m_impl.reset();
        if (close_status != Status::OK) return close_status;
    }
    return nemo::Result<void>::success();
}

Result<void> Database::flush() {
    if (m_impl && m_impl->state_ready) {
        // Phase 3: flush WAL first (WAL-before-data)
        if (m_impl->wal_manager) {
            try {
                m_impl->wal_manager->flush();
            } catch (const std::exception& error) {
                NEMO_LOG_ERROR("WAL", error.what());
                return Status::IOError;
            }
        }
        if (m_impl->shared_state) {
            auto persist_result = m_impl->shared_state->persist_state();
            if (!persist_result) return persist_result.error();
        }
        if (m_impl->buffer_pool) {
            auto flush_result = m_impl->buffer_pool->flush_all();
            if (!flush_result) return flush_result;
        }
        if (m_impl->disk_manager) {
            auto disk_flush_result = m_impl->disk_manager->flush();
            if (!disk_flush_result) return disk_flush_result;
        }
        if (m_impl->checkpoint_manager &&
            m_impl->checkpoint_manager->checkpoint_due() &&
            !m_impl->checkpoint_manager->perform_checkpoint()) {
            return Status::IOError;
        }
    }
    return nemo::Result<void>::success();
}

Result<void> Database::checkpoint() {
    if (!m_impl || !m_impl->state_ready) return {};
    auto flush_result = flush();
    if (!flush_result) return flush_result;
    if (!m_impl->checkpoint_manager ||
        !m_impl->checkpoint_manager->perform_checkpoint()) {
        return Status::IOError;
    }
    return {};
}

bool Database::is_open() const noexcept {
    return m_impl && m_impl->conn_pool != nullptr;
}

DatabaseStatistics Database::statistics() const {
    DatabaseStatistics stats;
    if (m_impl) {
        stats.active_connections = m_impl->conn_manager->active_connections_count();
        stats.worker_count       = m_impl->worker_pool->size();
        stats.busy_workers       = m_impl->worker_pool->active_workers();
        if (m_impl->buffer_pool) {
            stats.buffer_hits   = m_impl->buffer_pool->stats().cache_hits.load();
            stats.buffer_misses = m_impl->buffer_pool->stats().cache_misses.load();
            stats.buffer_pool_frames = m_impl->buffer_pool->frame_count();
        }
        stats.recovery_complete = m_impl->recovery_complete;  // Phase 4
    }
    return stats;
}

Result<void> Database::integrity_check() {
    return nemo::Result<void>::success();
}

Result<void> Database::backup(std::string_view /*path*/) {
    return Status::Unsupported;
}

std::string Database::engine_version() {
    return "0.2.3";
}

int Database::format_version() const {
    return 1;
}

} // namespace nemo
