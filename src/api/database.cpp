#include "nemo/api/database.hpp"
#include "nemo/api/connection.hpp"
#include "nemo/runtime/connection_manager.hpp"
#include "nemo/runtime/connection_pool.hpp"
#include "nemo/runtime/connection_lease.hpp"
#include "nemo/runtime/worker_pool.hpp"
#include "nemo/runtime/runtime_metrics.hpp"
#include "nemo/storage/disk_manager.hpp"
#include "nemo/storage/buffer_pool.hpp"
#include "nemo/catalog/catalog.hpp"

#include "api_internal.hpp"

namespace nemo {

class DatabaseImpl {
public:
    DatabaseConfig config;
    std::shared_ptr<api::SharedDatabaseState> shared_state;
    std::unique_ptr<storage::DiskManager> disk_manager;
    std::unique_ptr<storage::BufferPool> buffer_pool;
    std::unique_ptr<catalog::Catalog> catalog;
    
    runtime::RuntimeMetrics metrics;
    std::shared_ptr<runtime::ConnectionManager> conn_manager;
    std::shared_ptr<runtime::ConnectionPool> conn_pool;
    std::shared_ptr<runtime::WorkerPool> worker_pool;
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
    if (!path.empty()) {
        db.m_impl->shared_state->load_state();
    }
    db.m_impl->conn_manager = std::make_shared<runtime::ConnectionManager>();
    db.m_impl->conn_pool = std::make_shared<runtime::ConnectionPool>(db.m_impl->conn_manager, config.connection_pool_size);
    db.m_impl->worker_pool = std::make_shared<runtime::WorkerPool>(config.worker_count, 1024, db.m_impl->metrics);
    db.m_impl->worker_pool->start();
    
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
        if (m_impl->shared_state) {
            m_impl->shared_state->persist_state();
        }
        if (m_impl->worker_pool) {
            m_impl->worker_pool->shutdown();
            m_impl->conn_pool->shutdown();
            m_impl->conn_manager->shutdown();
        }
        m_impl.reset();
    }
    return nemo::Result<void>::success();
}

bool Database::is_open() const noexcept {
    return m_impl && m_impl->conn_pool != nullptr;
}

DatabaseStatistics Database::statistics() const {
    DatabaseStatistics stats;
    if (m_impl) {
        stats.active_connections = m_impl->conn_manager->active_connections_count();
        stats.worker_count = m_impl->worker_pool->size();
        stats.busy_workers = m_impl->worker_pool->active_workers();
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
    return "0.2.1";
}

int Database::format_version() const {
    return 1;
}

} // namespace nemo
