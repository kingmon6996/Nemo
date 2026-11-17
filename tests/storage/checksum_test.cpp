#include "nemo/core/config.hpp"
#include "nemo/storage/database_header.hpp"
#include "nemo/storage/disk_manager.hpp"
#include "nemo/storage/page_header.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

TEST(ChecksumTest, RejectsCorruptedPageOnRead) {
    const auto path = std::filesystem::temp_directory_path() /
        ("nemo_checksum_test_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".nm");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path.string() + ".lock", error);
        }
    } cleanup{path};

    nemo::Config config;
    config.set_page_size(8192);
    config.set_buffer_pool_size(32);
    auto created = nemo::storage::DiskManager::create(path, config);
    ASSERT_TRUE(created) << nemo::to_string(created.error());
    auto disk = std::move(created.value());
    auto allocated = disk->allocate_page();
    ASSERT_TRUE(allocated) << nemo::to_string(allocated.error());

    nemo::storage::Page page(allocated.value(), config.page_size());
    nemo::storage::PageHeader header{};
    header.page_id = allocated.value();
    header.page_type = nemo::storage::PageType::Metadata;
    header.header_version = 1;
    page.write_header(header);
    page.payload_span()[0] = std::byte{0x5a};
    ASSERT_TRUE(disk->write_page(page));
    ASSERT_TRUE(disk->flush());
    ASSERT_TRUE(disk->close());

    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(file.is_open());
        const auto offset = allocated.value().value() * config.page_size() +
                            nemo::storage::PageHeader::SIZE;
        file.seekp(static_cast<std::streamoff>(offset));
        file.put(static_cast<char>(0xa5));
        ASSERT_TRUE(file.good());
    }

    auto opened = nemo::storage::DiskManager::open(path, config);
    ASSERT_TRUE(opened) << nemo::to_string(opened.error());
    auto reopened = std::move(opened.value());
    const auto corrupted = reopened->read_page(allocated.value());
    ASSERT_FALSE(corrupted);
    EXPECT_EQ(corrupted.error(), nemo::Status::Corruption);
    EXPECT_TRUE(reopened->close());
}

}