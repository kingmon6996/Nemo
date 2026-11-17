#include "nemo/core/config.hpp"
#include "nemo/core/constants.hpp"
#include "nemo/storage/buffer_pool.hpp"
#include "nemo/storage/page_handle.hpp"
#include "nemo/storage/page_header.hpp"
#include "nemo/storage/table_page.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <barrier>
#include <filesystem>
#include <thread>
#include <vector>

namespace {

TEST(BufferPoolConfigTest, AppliesAutomaticBudgetAndClampsByteOverrides) {
    using nemo::core::BUFFER_POOL_MAX_BYTES;
    using nemo::core::BUFFER_POOL_MIN_BYTES;
    using nemo::core::clamp_buffer_pool_bytes;

    EXPECT_EQ(clamp_buffer_pool_bytes(0, 512ULL * 1024 * 1024),
              128ULL * 1024 * 1024);
    EXPECT_EQ(clamp_buffer_pool_bytes(1, 512ULL * 1024 * 1024),
              BUFFER_POOL_MIN_BYTES);
    EXPECT_EQ(clamp_buffer_pool_bytes(64ULL * 1024 * 1024, 0),
              64ULL * 1024 * 1024);
    EXPECT_EQ(clamp_buffer_pool_bytes(BUFFER_POOL_MAX_BYTES + 1, 0),
              BUFFER_POOL_MAX_BYTES);
    EXPECT_EQ(clamp_buffer_pool_bytes(0, 0), BUFFER_POOL_MIN_BYTES);
}

class BufferPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        path = std::filesystem::temp_directory_path() /
            ("nemo_buffer_pool_test_" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
             ".nm");
        config.set_page_size(8192);
        config.set_buffer_pool_size(2);
    }

    void TearDown() override {
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(path.string() + ".lock", error);
    }

    std::filesystem::path path;
    nemo::Config config;
};

TEST_F(BufferPoolTest, FlushesDirtyVictimsAndReloadsPagesAfterEviction) {
    auto disk_result = nemo::storage::DiskManager::create(path, config);
    ASSERT_TRUE(disk_result) << nemo::to_string(disk_result.error());
    auto disk = std::move(disk_result.value());
    std::vector<nemo::PageID> page_ids;

    {
        nemo::storage::BufferPool pool(2, disk.get());
        for (std::uint8_t marker = 1; marker <= 5; ++marker) {
            auto handle = pool.allocate_page();
            ASSERT_TRUE(handle) << nemo::to_string(handle.error());
            page_ids.push_back(handle.value().get().id());
            handle.value().get_mut().payload_span()[0] = static_cast<std::byte>(marker);
        }

        EXPECT_GT(pool.stats().evictions.load(), 0);
        EXPECT_GT(pool.stats().flushes.load(), 0);
        ASSERT_TRUE(pool.flush_all());

        for (std::uint8_t marker = 1; marker <= page_ids.size(); ++marker) {
            auto handle = pool.fetch_page(page_ids[marker - 1]);
            ASSERT_TRUE(handle) << nemo::to_string(handle.error());
            EXPECT_EQ(handle.value().get().payload_span()[0],
                      static_cast<std::byte>(marker));
        }
        EXPECT_GT(pool.stats().cache_misses.load(), page_ids.size());
    }

    EXPECT_TRUE(disk->close());
}

TEST_F(BufferPoolTest, FlushesAndReloadsSlottedTablePagesAfterEviction) {
    auto disk_result = nemo::storage::DiskManager::create(path, config);
    ASSERT_TRUE(disk_result) << nemo::to_string(disk_result.error());
    auto disk = std::move(disk_result.value());
    struct StoredRecord {
        nemo::PageID page_id;
        nemo::storage::RecordID record_id;
        std::byte marker;
    };
    std::vector<StoredRecord> records;

    {
        nemo::storage::BufferPool pool(2, disk.get());
        for (std::uint8_t marker = 1; marker <= 5; ++marker) {
            auto handle = pool.allocate_page();
            ASSERT_TRUE(handle) << nemo::to_string(handle.error());
            const auto page_id = handle.value().get().id();
            nemo::storage::TablePage table_page(std::move(handle.value()));
            table_page.init();
            const std::vector<std::byte> bytes{
                static_cast<std::byte>(marker), std::byte{0x5a}};
            auto inserted = table_page.insert(bytes);
            ASSERT_TRUE(inserted) << nemo::to_string(inserted.error());
            records.push_back({page_id, inserted.value(), bytes.front()});
        }

        EXPECT_GT(pool.stats().evictions.load(), 0);
        EXPECT_GT(pool.stats().flushes.load(), 0);
        for (const auto& record : records) {
            auto handle = pool.fetch_page(record.page_id);
            ASSERT_TRUE(handle) << nemo::to_string(handle.error());
            nemo::storage::TablePage table_page(std::move(handle.value()));
            auto fetched = table_page.get(record.record_id);
            ASSERT_TRUE(fetched) << nemo::to_string(fetched.error());
            ASSERT_EQ(fetched.value().size(), 2);
            EXPECT_EQ(fetched.value()[0], record.marker);
            EXPECT_EQ(fetched.value()[1], std::byte{0x5a});
        }
        EXPECT_TRUE(pool.flush_all());
    }

    EXPECT_TRUE(disk->close());
}

TEST_F(BufferPoolTest, RefusesEvictionWhenEveryFrameIsPinned) {
    auto disk_result = nemo::storage::DiskManager::create(path, config);
    ASSERT_TRUE(disk_result) << nemo::to_string(disk_result.error());
    auto disk = std::move(disk_result.value());
    auto second_id = disk->allocate_page();
    ASSERT_TRUE(second_id);
    {
        nemo::storage::BufferPool pool(1, disk.get());
        {
            auto first = pool.allocate_page();
            ASSERT_TRUE(first);
            auto blocked = pool.fetch_page(second_id.value());
            ASSERT_FALSE(blocked);
            EXPECT_EQ(blocked.error(), nemo::Status::InternalError);
        }

        {
            auto fetched = pool.fetch_page(second_id.value());
            ASSERT_TRUE(fetched) << nemo::to_string(fetched.error());
            EXPECT_EQ(fetched.value().get().id(), second_id.value());
        }
        EXPECT_TRUE(pool.flush_all());
    }
    EXPECT_TRUE(disk->close());
}

TEST_F(BufferPoolTest, ConcurrentMissesWaitForPageInitialization) {
    auto disk_result = nemo::storage::DiskManager::create(path, config);
    ASSERT_TRUE(disk_result) << nemo::to_string(disk_result.error());
    auto disk = std::move(disk_result.value());
    std::vector<nemo::PageID> page_ids;
    for (std::uint8_t marker = 1; marker <= 3; ++marker) {
        auto allocated = disk->allocate_page();
        ASSERT_TRUE(allocated);
        auto page = disk->read_page(allocated.value());
        ASSERT_TRUE(page);
        page.value().payload_span()[0] = static_cast<std::byte>(marker);
        ASSERT_TRUE(disk->write_page(page.value()));
        page_ids.push_back(allocated.value());
    }

    constexpr std::size_t thread_count = 8;
    std::barrier start(static_cast<std::ptrdiff_t>(thread_count));
    std::atomic<std::size_t> failures{0};
    {
        nemo::storage::BufferPool pool(page_ids.size(), disk.get());
        std::vector<std::thread> readers;
        readers.reserve(thread_count);
        for (std::size_t thread = 0; thread < thread_count; ++thread) {
            readers.emplace_back([&, thread] {
                start.arrive_and_wait();
                for (std::size_t pass = 0; pass < 50; ++pass) {
                    for (std::size_t page = 0; page < page_ids.size(); ++page) {
                        auto fetched = pool.fetch_page(page_ids[(page + thread) % page_ids.size()]);
                        if (!fetched) {
                            failures.fetch_add(1, std::memory_order_relaxed);
                            continue;
                        }
                        const auto expected = static_cast<std::byte>(
                            ((page + thread) % page_ids.size()) + 1);
                        if (fetched.value().get().payload_span()[0] != expected) {
                            failures.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }
            });
        }
        for (auto& reader : readers) reader.join();
        EXPECT_EQ(failures.load(), 0);
        EXPECT_TRUE(pool.flush_all());
    }
    EXPECT_TRUE(disk->close());
}

}