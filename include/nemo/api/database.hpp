#ifndef NEMO_API_DATABASE_HPP
#define NEMO_API_DATABASE_HPP

#include "nemo/core/result.hpp"
#include <string_view>
#include <string>
#include <memory>

namespace nemo {

class DatabaseImpl;
namespace api {
    class Connection;
    struct ConnectionConfig;
}

struct DatabaseConfig {
    std::size_t buffer_pool_size{1024 * 1024 * 1024}; // 1GB default
    std::size_t worker_count{4};
    std::size_t connection_pool_size{16};
};

struct DatabaseStatistics {
    std::size_t active_connections{0};
    std::size_t active_transactions{0};
    std::size_t active_queries{0};
    std::size_t worker_count{0};
    std::size_t busy_workers{0};
    std::size_t buffer_hits{0};
    std::size_t buffer_misses{0};
};

// Main entry point for interacting with Nemo.
class Database {
public:
    // Disallow copying, allow moving
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;
    
    ~Database();

    // Open an existing database file
        static Result<Database> open(std::string_view path, const DatabaseConfig& config = DatabaseConfig{});
    static Result<Database> create(std::string_view path, const DatabaseConfig& config = DatabaseConfig{});

    // Create a new database file
    

    // Close the database connection safely
    Result<void> close();

    // Check if the database is open
        bool is_open() const noexcept;
    
    // Connect to the database
    Result<api::Connection> connect(const api::ConnectionConfig& config);
    
    // Statistics
    DatabaseStatistics statistics() const;
    
    // Check integrity
    Result<void> integrity_check();
    
    // Backup
    Result<void> backup(std::string_view path);
    
    // Versions
    static std::string engine_version();
    int format_version() const;

private:
    Database();

    // Pimpl idiom to hide internal details
    std::unique_ptr<DatabaseImpl> m_impl;
};

} // namespace nemo

#endif // NEMO_API_DATABASE_HPP

