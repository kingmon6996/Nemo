#include "nemo/storage/disk_manager.hpp"
#include "nemo/storage/checksum.hpp"
#include "nemo/logging/logger.hpp"
#include "nemo/storage/serialization.hpp"
#include "nemo/wal/wal_manager.hpp"
#include <algorithm>
#include <array>
#include <random>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace nemo::storage {
namespace {

constexpr std::array<std::byte, 4> FREE_BITMAP_MAGIC{
    std::byte{0x4E}, std::byte{0x46}, std::byte{0x52}, std::byte{0x31}};
constexpr std::size_t FREE_BITMAP_HEADER_SIZE = 32;

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

void append_page_bytes(std::vector<std::uint8_t>& bytes,
                       std::span<const std::byte> page_bytes) {
    for (const auto value : page_bytes) {
        bytes.push_back(std::to_integer<std::uint8_t>(value));
    }
}

}

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
    , m_wal_manager(other.m_wal_manager)
    , m_is_open(other.m_is_open)
{
    other.m_wal_manager = nullptr;
    other.m_is_open = false;
}

DiskManager& DiskManager::operator=(DiskManager&& other) noexcept {
    if (this != &other) {
        (void)close();
        m_config = std::move(other.m_config);
        m_db_header = std::move(other.m_db_header);
        m_file_manager = std::move(other.m_file_manager);
        m_free_page_manager = std::move(other.m_free_page_manager);
        m_wal_manager = other.m_wal_manager;
        m_is_open = other.m_is_open;
        other.m_wal_manager = nullptr;
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
    m_is_open = true;

    // Setup Database Header
    m_db_header.magic = MAGIC_BYTES;
    m_db_header.format_version = 2; // STORAGE_FORMAT_VERSION
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
    m_db_header.metadata_root_page = PageID(0);
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
    if (m_db_header.format_version != 1 && m_db_header.format_version != 2) {
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
    if (m_db_header.format_version == 1) {
        m_db_header.metadata_root_page = m_db_header.catalog_root_page;
        m_db_header.catalog_root_page = PageID(0);
        m_db_header.format_version = 2;
    }

    return load_free_page_bitmap();
}

Result<void> DiskManager::close() {
    if (!m_is_open) return Result<void>::success();

    auto flush_res = flush();
    auto close_res = m_file_manager.close();
    
    m_is_open = false;
    
    if (!flush_res) return flush_res;
    return close_res;
}

Result<void> DiskManager::close_without_flush() {
    if (!m_is_open) return Result<void>::success();
    auto close_result = m_file_manager.close();
    m_is_open = false;
    return close_result;
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
    return write_page_impl(page, true);
}

Result<void> DiskManager::write_page_from_recovery(const Page& page) {
    return write_page_impl(page, false);
}

Result<void> DiskManager::write_page_impl(const Page& page, bool log_page_image) {
    std::lock_guard<std::recursive_mutex> write_lock(m_write_mutex);
    if (!m_is_open) return Result<void>(Status::InvalidState);
    if (page.data().size() != m_config.page_size()) {
        return Result<void>(Status::InvalidArgument);
    }

    auto offset_res = calculate_offset(page.id());
    if (!offset_res) return Result<void>(offset_res.error());

    Page& mutable_page = const_cast<Page&>(page);
    PageHeader header{};
    if (page.id().value() != 0) {
        header = page.read_header();
    }

    std::vector<std::byte> before(page.data().size(), std::byte{0});
    if (log_page_image && m_wal_manager != nullptr) {
        auto current_size = m_file_manager.file_size();
        if (!current_size) return Result<void>(current_size.error());
        const auto page_end = offset_res.value() + page.data().size();
        if (page_end < offset_res.value()) {
            return Result<void>(Status::InvalidArgument);
        }
        if (page_end <= current_size.value()) {
            auto read_result = m_file_manager.read(offset_res.value(), before);
            if (!read_result) return read_result;
        } else if (offset_res.value() < current_size.value()) {
            return Result<void>(Status::Corruption);
        } else if (page.id().value() != 0) {
            Page unallocated_page(page.id(), m_config.page_size());
            PageHeader unallocated_header{};
            unallocated_header.page_id = page.id();
            unallocated_header.page_type = PageType::Invalid;
            unallocated_page.write_header(unallocated_header);
            unallocated_header.checksum = calculate_checksum(unallocated_page.data());
            unallocated_page.write_header(unallocated_header);
            std::copy(unallocated_page.data().begin(), unallocated_page.data().end(),
                      before.begin());
        }

        std::vector<std::uint8_t> payload;
        payload.reserve(12 + before.size() + page.data().size());
        append_u64(payload, page.id().value());
        append_u32(payload, static_cast<std::uint32_t>(page.data().size()));
        append_page_bytes(payload, before);
        append_page_bytes(payload, page.data());

        const auto txn_id = m_wal_manager->current_transaction_id();
        wal::LogRecord record(txn_id, wal::LogRecordType::PageWrite,
                              wal::LSN::invalid(), std::move(payload));
        wal::LSN lsn;
        try {
            lsn = m_wal_manager->append(record);
            if (page.id().value() != 0) {
                header.lsn = lsn.value();
                header.checksum = 0;
                mutable_page.write_header(header);
            }
            m_wal_manager->flush_up_to(lsn);
        } catch (const std::exception& error) {
            NEMO_LOG_ERROR(
                "Storage", std::string("Failed to write page image to WAL: ") + error.what());
            return Result<void>(Status::IOError);
        }
    }

    if (page.id().value() != 0) {
        header.checksum = 0;
        mutable_page.write_header(header);
        header.checksum = calculate_checksum(page.data());
        mutable_page.write_header(header);
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

    auto page_result = read_page(page_id);
    if (!page_result) return page_result.error();
    if (page_result.value().read_header().page_type == PageType::FreeSpace) {
        return Result<void>(Status::InvalidArgument);
    }
    return m_free_page_manager.release(page_id);
}

Result<void> DiskManager::flush() {
    if (!m_is_open) return Result<void>(Status::InvalidState);

    auto bitmap_result = persist_free_page_bitmap();
    if (!bitmap_result) return bitmap_result;

    Page page0(PageID(0), m_config.page_size());
    m_db_header.checksum = 0;
    m_db_header.serialize(page0.data());
    m_db_header.checksum = calculate_checksum(page0.data().first(DatabaseHeader::SIZE));
    m_db_header.serialize(page0.data());
    auto write_result = write_page(page0);
    if (!write_result) return write_result;
    return m_file_manager.flush();
}

Result<void> DiskManager::reload_after_recovery() {
    auto page_result = read_page(PageID(0));
    if (!page_result) return page_result.error();
    Page& page0 = page_result.value();
    m_db_header.deserialize(page0.data());

    if (m_db_header.magic != MAGIC_BYTES ||
        m_db_header.page_size != m_config.page_size()) {
        return Status::Corruption;
    }
    if (m_db_header.format_version != 1 && m_db_header.format_version != 2) {
        return Status::Unsupported;
    }

    const auto stored_checksum = m_db_header.checksum;
    m_db_header.checksum = 0;
    m_db_header.serialize(page0.data());
    const auto calculated_checksum =
        calculate_checksum(page0.data().first(DatabaseHeader::SIZE));
    if (stored_checksum != calculated_checksum) return Status::Corruption;

    m_db_header.checksum = stored_checksum;
    m_db_header.deserialize(page0.data());
    if (m_db_header.format_version == 1) {
        m_db_header.metadata_root_page = m_db_header.catalog_root_page;
        m_db_header.catalog_root_page = PageID(0);
        m_db_header.format_version = 2;
    }
    return load_free_page_bitmap();
}

Result<void> DiskManager::load_free_page_bitmap() {
    if (m_db_header.free_page_root.value() == 0) {
        return m_free_page_manager.restore({});
    }

    std::vector<Page> pages;
    std::unordered_set<std::uint64_t> seen;
    std::uint64_t total_pages = 0;
    PageID current = m_db_header.free_page_root;
    while (current.is_valid()) {
        if (current.value() == 0 || !seen.insert(current.value()).second) {
            return Status::Corruption;
        }
        auto page_result = read_page(current);
        if (!page_result) return page_result.error();
        Page page = std::move(page_result.value());
        const auto header = page.read_header();
        if (header.page_id != current || header.page_type != PageType::FreeSpace ||
            header.payload_size < FREE_BITMAP_HEADER_SIZE ||
            header.payload_size > page.payload_span().size()) {
            return Status::Corruption;
        }
        const auto payload = page.payload_span();
        if (!std::equal(FREE_BITMAP_MAGIC.begin(), FREE_BITMAP_MAGIC.end(), payload.begin())) {
            return Status::Corruption;
        }
        const auto chunk_bytes = read_u32_le(payload, 4);
        const auto next = PageID(read_u64_le(payload, 8));
        const auto stored_total_pages = read_u64_le(payload, 16);
        const auto start_byte = read_u64_le(payload, 24);
        const auto bitmap_bytes = (stored_total_pages + 7) / 8;
        if (stored_total_pages == 0 ||
            stored_total_pages > m_db_header.next_page_id.value() ||
            header.payload_size < FREE_BITMAP_HEADER_SIZE + chunk_bytes ||
            start_byte > bitmap_bytes || chunk_bytes > bitmap_bytes - start_byte ||
            (total_pages != 0 && total_pages != stored_total_pages)) {
            return Status::Corruption;
        }
        total_pages = stored_total_pages;
        pages.push_back(std::move(page));
        current = next;
    }

    const auto bitmap_bytes = static_cast<std::size_t>((total_pages + 7) / 8);
    std::vector<std::byte> bitmap(bitmap_bytes, std::byte{0});
    std::size_t copied = 0;
    for (const auto& page : pages) {
        const auto payload = page.payload_span();
        const auto chunk_bytes = read_u32_le(payload, 4);
        const auto start_byte = static_cast<std::size_t>(read_u64_le(payload, 24));
        if (start_byte != copied || chunk_bytes > bitmap.size() - copied) {
            return Status::Corruption;
        }
        std::copy_n(payload.begin() + static_cast<std::ptrdiff_t>(FREE_BITMAP_HEADER_SIZE),
                    chunk_bytes, bitmap.begin() + static_cast<std::ptrdiff_t>(copied));
        copied += chunk_bytes;
    }
    if (copied != bitmap_bytes) return Status::Corruption;

    std::vector<PageID> free_pages;
    for (std::uint64_t page_id = 1; page_id < total_pages; ++page_id) {
        const auto byte = static_cast<std::size_t>(page_id / 8);
        const auto bit = static_cast<unsigned int>(page_id % 8);
        if ((std::to_integer<std::uint8_t>(bitmap[byte]) & (1U << bit)) != 0) {
            if (seen.contains(page_id)) return Status::Corruption;
            free_pages.emplace_back(page_id);
        }
    }
    return m_free_page_manager.restore(std::move(free_pages));
}

Result<void> DiskManager::persist_free_page_bitmap() {
    if (m_config.page_size() <= PageHeader::SIZE + FREE_BITMAP_HEADER_SIZE) {
        return Status::InvalidState;
    }
    const std::uint64_t bitmap_bytes_per_page =
        m_config.page_size() - PageHeader::SIZE - FREE_BITMAP_HEADER_SIZE;
    const std::uint64_t bits_per_page = bitmap_bytes_per_page * 8;
    std::vector<PageID> bitmap_page_ids;

    if (m_db_header.free_page_root.value() != 0) {
        std::unordered_set<std::uint64_t> seen;
        PageID current = m_db_header.free_page_root;
        while (current.is_valid()) {
            if (current.value() == 0 || !seen.insert(current.value()).second) {
                return Status::Corruption;
            }
            auto page_result = read_page(current);
            if (!page_result) return page_result.error();
            const auto header = page_result.value().read_header();
            if (header.page_type != PageType::FreeSpace ||
                header.payload_size < FREE_BITMAP_HEADER_SIZE) {
                return Status::Corruption;
            }
            bitmap_page_ids.push_back(current);
            current = PageID(read_u64_le(page_result.value().payload_span(), 8));
        }
    }

    std::uint64_t required_pages = 0;
    do {
        required_pages = std::max<std::uint64_t>(
            1, (m_db_header.next_page_id.value() + bits_per_page - 1) / bits_per_page);
        if (bitmap_page_ids.size() >= required_pages) break;
        const PageID new_id = m_db_header.next_page_id;
        if (new_id.value() == std::numeric_limits<std::uint64_t>::max()) {
            return Status::InvalidState;
        }
        m_db_header.next_page_id = PageID(new_id.value() + 1);
        Page page(new_id, m_config.page_size());
        PageHeader header{};
        header.page_id = new_id;
        header.page_type = PageType::Invalid;
        page.write_header(header);
        auto write_result = write_page(page);
        if (!write_result) return write_result;
        bitmap_page_ids.push_back(new_id);
    } while (true);

    const std::uint64_t total_pages = m_db_header.next_page_id.value();
    const std::uint64_t total_bitmap_bytes = (total_pages + 7) / 8;
    if (total_bitmap_bytes > std::numeric_limits<std::size_t>::max()) {
        return Status::InvalidState;
    }
    std::vector<std::byte> bitmap(static_cast<std::size_t>(total_bitmap_bytes), std::byte{0});
    std::unordered_set<std::uint64_t> metadata_ids;
    metadata_ids.reserve(bitmap_page_ids.size());
    for (const auto id : bitmap_page_ids) metadata_ids.insert(id.value());
    for (const auto id : m_free_page_manager.snapshot()) {
        if (id.value() == 0 || id.value() >= total_pages || metadata_ids.contains(id.value())) {
            return Status::Corruption;
        }
        const auto byte = static_cast<std::size_t>(id.value() / 8);
        const auto bit = static_cast<unsigned int>(id.value() % 8);
        bitmap[byte] |= static_cast<std::byte>(1U << bit);
    }

    m_db_header.free_page_root = bitmap_page_ids.front();
    std::size_t offset = 0;
    for (std::size_t i = 0; i < bitmap_page_ids.size(); ++i) {
        auto page_result = read_page(bitmap_page_ids[i]);
        if (!page_result) return page_result.error();
        Page page = std::move(page_result.value());
        auto payload = page.payload_span();
        const auto chunk_size = std::min<std::size_t>(
            static_cast<std::size_t>(bitmap_bytes_per_page), bitmap.size() - offset);
        std::copy(FREE_BITMAP_MAGIC.begin(), FREE_BITMAP_MAGIC.end(), payload.begin());
        write_u32_le(payload, 4, static_cast<std::uint32_t>(chunk_size));
        const PageID next = i + 1 < bitmap_page_ids.size()
            ? bitmap_page_ids[i + 1] : PageID{};
        write_u64_le(payload, 8, next.value());
        write_u64_le(payload, 16, total_pages);
        write_u64_le(payload, 24, offset);
        std::copy_n(bitmap.begin() + static_cast<std::ptrdiff_t>(offset), chunk_size,
                    payload.begin() + static_cast<std::ptrdiff_t>(FREE_BITMAP_HEADER_SIZE));
        std::fill(payload.begin() + static_cast<std::ptrdiff_t>(
                      FREE_BITMAP_HEADER_SIZE + chunk_size), payload.end(), std::byte{0});
        offset += chunk_size;

        auto header = page.read_header();
        header.page_type = PageType::FreeSpace;
        header.header_version = 1;
        header.payload_size = static_cast<std::uint32_t>(FREE_BITMAP_HEADER_SIZE + chunk_size);
        page.write_header(header);
        auto write_result = write_page(page);
        if (!write_result) return write_result;
    }
    return {};
}

std::uint64_t DiskManager::file_size() const {
    auto size_res = m_file_manager.file_size();
    if (size_res) {
        return size_res.value();
    }
    return 0;
}

} // namespace nemo::storage
