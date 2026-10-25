#ifndef NEMO_STORAGE_DISK_MANAGER_HPP
#define NEMO_STORAGE_DISK_MANAGER_HPP

#include "nemo/core/result.hpp"
#include "nemo/core/config.hpp"
#include "nemo/storage/page.hpp"
#include "nemo/storage/database_header.hpp"
#include "nemo/storage/file_manager.hpp"
#include "nemo/storage/free_page_manager.hpp"
#include <filesystem>
#include <memory>

namespace nemo::storage {

class DiskManager {
public:
    // Factory method for creating a new database
    static Result<std::unique_ptr<DiskManager>> create(
        const std::filesystem::path& path,
        const Config& config
    );

    // Factory method for opening an existing database
    static Result<std::unique_ptr<DiskManager>> open(
        const std::filesystem::path& path,
        const Config& config
    );

    ~DiskManager();

    // Disable copy, allow move
    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;
    DiskManager(DiskManager&&) noexcept;
    DiskManager& operator=(DiskManager&&) noexcept;

    Result<void> close();

    Result<Page> read_page(PageID page_id);
    Result<void> write_page(const Page& page);

    Result<PageID> allocate_page();
    Result<void> deallocate_page(PageID page_id);

    Result<void> flush();
    std::uint64_t file_size() const;

    const DatabaseHeader& get_database_header() const { return m_db_header; }

private:
    DiskManager(const Config& config);

    Result<void> init_new_database(const std::filesystem::path& path);
    Result<void> load_existing_database(const std::filesystem::path& path);
    Result<std::uint64_t> calculate_offset(PageID page_id) const;

    Config m_config;
    DatabaseHeader m_db_header;
    FileManager m_file_manager;
    FreePageManager m_free_page_manager;
    bool m_is_open{false};
};

} // namespace nemo::storage

#endif // NEMO_STORAGE_DISK_MANAGER_HPP
