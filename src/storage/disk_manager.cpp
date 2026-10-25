#include "nemo/storage/disk_manager.hpp"
#include "nemo/storage/checksum.hpp"
#include "nemo/logging/logger.hpp"
#include <random>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace nemo::storage {

DiskManager::DiskManager(const Config& config) 
    : m_config(config), m_db_header{} {}

DiskManager::~DiskManager() {
    (void)close();
}

DiskManager::DiskManager(DiskManager&& other) noexcept
    : m_config(std::move(other.m_config))
    , m_db_header(std::move(other.m_db_header))
    , m_file_manager(std::move(other.m_file_manager))
    , m_free_page_manager(std::move(other.m_free_page_manager))
    , m_is_open(other.m_is_open)
{
    other.m_is_open = false;
}

DiskManager& DiskManager::operator=(DiskManager&& other) noexcept {
    if (this != &other) {
        (void)close();
        m_config = std::move(other.m_config);
        m_db_header = std::move(other.m_db_header);
        m_file_manager = std::move(other.m_file_manager);
        m_free_page_manager = std::move(other.m_free_page_manager);
        m_is_open = other.m_is_open;
        other.m_is_open = false;
    }
    return *this;
}

Result<std::unique_ptr<DiskManager>> DiskManager::create(
    const std::filesystem::path& path,
    const Config& config
) {
    auto manager = std::unique_ptr<DiskManager>(new DiskManager(config));
    auto init_result = manager->init_new_database(path);
    if (!init_result) {
        return Result<std::unique_ptr<DiskManager>>(init_result.error());
    }
    return manager;
}

Result<std::unique_ptr<DiskManager>> DiskManager::open(
    const std::filesystem::path& path,
    const Config& config
) {
    auto manager = std::unique_ptr<DiskManager>(new DiskManager(config));
    auto load_result = manager->load_existing_database(path);
    if (!load_result) {
        return Result<std::unique_ptr<DiskManager>>(load_result.error());
    }
    return manager;
}

Result<void> DiskManager::init_new_database(const std::filesystem::path& path) {
    auto create_res = m_file_manager.create_file(path);
    if (!create_res) return create_res;

    // Setup Database Header
    m_db_header.magic = MAGIC_BYTES;
    m_db_header.format_version = 1; // STORAGE_FORMAT_VERSION
    m_db_header.page_size = static_cast<std::uint32_t>(m_config.page_size());
    
    // Generate simple random UUID
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uint64_t uuid_part1 = gen();
    std::uint64_t uuid_part2 = gen();
    std::memcpy(m_db_header.database_id.data(), &uuid_part1, 8);
    std::memcpy(m_db_header.database_id.data() + 8, &uuid_part2, 8);

    m_db_header.creation_timestamp = 0; // TODO: get real timestamp if needed
    m_db_header.catalog_root_page = PageID(0); // INVALID_PAGE_ID semantics (0 is header)
    m_db_header.free_page_root = PageID(0);
    m_db_header.next_page_id = PageID(1); // Page 0 is header
    m_db_header.flags = 0;
    
    // Calculate header checksum
    Page page0(PageID(0), m_config.page_size());
    m_db_header.checksum = 0; // zero before checksumming
    m_db_header.serialize(page0.data());
    
    std::span<const std::byte> header_span = page0.data().first(DatabaseHeader::SIZE);
    m_db_header.checksum = calculate_checksum(header_span);
    m_db_header.serialize(page0.data());

    auto write_res = write_page(page0);
    if (!write_res) return write_res;

    m_is_open = true;
    return flush();
}

Result<void> DiskManager::load_existing_database(const std::filesystem::path& path) {
    auto open_res = m_file_manager.open_file(path);
    if (!open_res) return open_res;

    auto size_res = m_file_manager.file_size();
    if (!size_res) return Result<void>(size_res.error());

    if (size_res.value() % m_config.page_size() != 0) {
        NEMO_LOG_ERROR("Storage", "File size is not aligned to page size");
        return Result<void>(Status::Corruption);
    }

    if (size_res.value() < m_config.page_size()) {
        NEMO_LOG_ERROR("Storage", "File is smaller than a single page");
        return Result<void>(Status::Corruption);
    }

    m_is_open = true; // Need to be true for read_page to work
    
    auto page0_res = read_page(PageID(0));
    if (!page0_res) {
        m_is_open = false;
        return Result<void>(page0_res.error());
    }

    Page& page0 = page0_res.value();
    m_db_header.deserialize(page0.data());

    // Validation
    if (m_db_header.magic != MAGIC_BYTES) {
        m_is_open = false;
        NEMO_LOG_ERROR("Storage", "Invalid Magic Bytes");
        return Result<void>(Status::Corruption);
    }

    // Validate Header
    if (m_db_header.format_version != 1) { // STORAGE_FORMAT_VERSION
        m_is_open = false;
        NEMO_LOG_ERROR("Storage", "Invalid format version");
        return Result<void>(Status::Unsupported);
    }

    if (m_db_header.page_size != m_config.page_size()) {
        m_is_open = false;
        NEMO_LOG_ERROR("Storage", "Page size mismatch");
        return Result<void>(Status::Corruption);
    }

    // Verify Checksum
    std::uint32_t stored_checksum = m_db_header.checksum;
    m_db_header.checksum = 0;
    m_db_header.serialize(page0.data());
    std::uint32_t calculated_checksum = calculate_checksum(page0.data().first(DatabaseHeader::SIZE));
    
    if (stored_checksum != calculated_checksum) {
        m_is_open = false;
        NEMO_LOG_ERROR("Storage", "Database header checksum mismatch");
        return Result<void>(Status::Corruption);
    }
    
    m_db_header.checksum = stored_checksum;
    m_db_header.deserialize(page0.data());

    return Result<void>::success();
}

Result<void> DiskManager::close() {
    if (!m_is_open) return Result<void>::success();

    // Flush metadata page
    Page page0(PageID(0), m_config.page_size());
    m_db_header.checksum = 0;
    m_db_header.serialize(page0.data());
    m_db_header.checksum = calculate_checksum(page0.data().first(DatabaseHeader::SIZE));
    m_db_header.serialize(page0.data());

    auto write_res = write_page(page0);
    if (!write_res) {
        NEMO_LOG_ERROR("Storage", "Failed to flush database header on close");
        // Continue closing file manager anyway
    }

    auto flush_res = m_file_manager.flush();
    auto close_res = m_file_manager.close();
    
    m_is_open = false;
    
    if (!flush_res) return flush_res;
    return close_res;
}

Result<std::uint64_t> DiskManager::calculate_offset(PageID page_id) const {
    if (!page_id.is_valid()) {
        return Result<std::uint64_t>(Status::InvalidArgument);
    }
    
    std::uint64_t offset = page_id.value() * m_config.page_size();
    
    // Overflow check
    if (offset / m_config.page_size() != page_id.value()) {
        return Result<std::uint64_t>(Status::InvalidArgument);
    }
    
    return offset;
}

Result<Page> DiskManager::read_page(PageID page_id) {
    if (!m_is_open) return Result<Page>(Status::InvalidState);

    auto offset_res = calculate_offset(page_id);
    if (!offset_res) return Result<Page>(offset_res.error());

    Page page(page_id, m_config.page_size());
    auto read_res = m_file_manager.read(offset_res.value(), page.data());
    if (!read_res) return Result<Page>(read_res.error());

    // Page 0 doesn't have a standard PageHeader, it's the DatabaseHeader.
    if (page_id.value() != 0) {
        PageHeader header = page.read_header();
        
        // Checksum verification
        std::uint32_t stored_checksum = header.checksum;
        header.checksum = 0;
        page.write_header(header);
        
        std::uint32_t calculated = calculate_checksum(page.data());
        if (stored_checksum != calculated) {
            NEMO_LOG_ERROR("Storage", "Page checksum mismatch");
            return Result<Page>(Status::Corruption);
        }
        
        // Restore
        header.checksum = stored_checksum;
        page.write_header(header);
        page.clear_dirty();
    }

    return page;
}

Result<void> DiskManager::write_page(const Page& page) {
    if (!m_is_open) return Result<void>(Status::InvalidState);

    auto offset_res = calculate_offset(page.id());
    if (!offset_res) return Result<void>(offset_res.error());

    if (page.id().value() != 0) {
        PageHeader header = page.read_header();
        header.checksum = 0;
        // Non-const cast for serialization purposes internally before writing
        const_cast<Page&>(page).write_header(header);
        
        header.checksum = calculate_checksum(page.data());
        const_cast<Page&>(page).write_header(header);
    }

    return m_file_manager.write(offset_res.value(), page.data());
}

Result<PageID> DiskManager::allocate_page() {
    if (!m_is_open) return Result<PageID>(Status::InvalidState);

    auto free_res = m_free_page_manager.allocate();
    if (free_res) {
        return free_res.value();
    }

    PageID new_id = m_db_header.next_page_id;
    m_db_header.next_page_id = PageID(new_id.value() + 1);

    // Initialize with zeroes and write it so file size increases
    Page empty_page(new_id, m_config.page_size());
    PageHeader empty_header{};
    empty_header.page_id = new_id;
    empty_header.page_type = PageType::Invalid;
    empty_page.write_header(empty_header);

    auto write_res = write_page(empty_page);
    if (!write_res) return Result<PageID>(write_res.error());

    return new_id;
}

Result<void> DiskManager::deallocate_page(PageID page_id) {
    if (!m_is_open) return Result<void>(Status::InvalidState);
    
    if (page_id.value() == 0) {
        return Result<void>(Status::InvalidArgument); // Page 0 is protected
    }

    return m_free_page_manager.release(page_id);
}

Result<void> DiskManager::flush() {
    return m_file_manager.flush();
}

std::uint64_t DiskManager::file_size() const {
    auto size_res = m_file_manager.file_size();
    if (size_res) {
        return size_res.value();
    }
    return 0;
}

} // namespace nemo::storage
