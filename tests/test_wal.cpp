#include "nemo/wal/log_reader.hpp"
#include "nemo/wal/wal_manager.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace nemo::wal {

class WALManagerTestAccess {
public:
    static bool should_wait_for_group_commit(std::size_t buffered_bytes) {
        return WALManager::should_wait_for_group_commit(buffered_bytes);
    }

    static void inject_write_failure(WALManager& manager) {
        manager.m_before_segment_append_for_testing = [] {
            throw std::runtime_error("Injected WAL write failure.");
        };
    }

    static void inject_fsync_failure(WALManager& manager) {
        manager.m_before_segment_flush_for_testing = [] {
            throw std::runtime_error("Injected WAL fsync failure.");
        };
    }
};

} // namespace nemo::wal

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
            ("nemo_wal_test_" + std::to_string(nonce));
        std::filesystem::create_directories(m_path);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    const std::filesystem::path& path() const noexcept {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

nemo::wal::LogRecord make_record(std::uint64_t txn_id) {
    return nemo::wal::LogRecord(
        txn_id, nemo::wal::LogRecordType::TxnBegin, nemo::wal::LSN::invalid());
}

} // namespace

TEST(WALManagerTest, FlushUpToMakesItsTargetDurable) {
    TemporaryDirectory directory;
    nemo::wal::WALManager manager(directory.path().string(), nemo::wal::DurabilityMode::Full);
    auto record = make_record(1);

    const auto lsn = manager.append(record);
    EXPECT_EQ(lsn.value(), 1);
    manager.flush_up_to(lsn);

    EXPECT_EQ(manager.get_flushed_lsn().value(), lsn.value());
}

TEST(WALManagerTest, ReopenContinuesTheLsnSequence) {
    TemporaryDirectory directory;
    {
        nemo::wal::WALManager manager(
            directory.path().string(), nemo::wal::DurabilityMode::Full);
        auto record = make_record(1);
        const auto lsn = manager.append(record);
        manager.flush_up_to(lsn);
    }

    {
        nemo::wal::WALManager manager(
            directory.path().string(), nemo::wal::DurabilityMode::Full);
        auto record = make_record(2);
        const auto lsn = manager.append(record);
        EXPECT_EQ(lsn.value(), 2);
        manager.flush_up_to(lsn);
    }

    nemo::wal::LogReader reader(directory.path().string());
    ASSERT_TRUE(reader.initialize());
    ASSERT_TRUE(reader.seek_to_first());
    const auto first = reader.read_next();
    const auto second = reader.read_next();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->get_lsn().value(), 1);
    EXPECT_EQ(second->get_lsn().value(), 2);
    EXPECT_FALSE(reader.read_next().has_value());
}

TEST(WALManagerTest, OversizedRecordDoesNotConsumeAnLsn) {
    TemporaryDirectory directory;
    nemo::wal::WALManager manager(
        directory.path().string(), nemo::wal::DurabilityMode::Full);
    nemo::wal::LogRecord oversized(
        1, nemo::wal::LogRecordType::TxnBegin, nemo::wal::LSN::invalid(),
        std::vector<std::uint8_t>(
            nemo::wal::WALManager::BUFFER_CAPACITY, 0));
    EXPECT_THROW(manager.append(oversized), std::length_error);

    auto record = make_record(1);
    EXPECT_EQ(manager.append(record).value(), 1);
}

TEST(WALManagerTest, RejectsAnUnappendedFlushTarget) {
    TemporaryDirectory directory;
    nemo::wal::WALManager manager(
        directory.path().string(), nemo::wal::DurabilityMode::Full);

    EXPECT_THROW(manager.flush_up_to(nemo::wal::LSN(1)), std::invalid_argument);
}

TEST(WALManagerTest, NormalModeWaitsForTheGroupCommitWindow) {
    TemporaryDirectory directory;
    nemo::wal::WALManager manager(
        directory.path().string(), nemo::wal::DurabilityMode::Normal);
    auto record = make_record(1);
    const auto started = std::chrono::steady_clock::now();

    const auto lsn = manager.append(record);
    manager.flush_up_to(lsn);

    EXPECT_GE(
        std::chrono::steady_clock::now() - started,
        nemo::wal::WALManager::GROUP_COMMIT_WINDOW);
}

TEST(WALManagerTest, FourMegabyteThresholdSkipsTheBatchWait) {
    const auto threshold = nemo::wal::WALManager::GROUP_COMMIT_SIZE;

    EXPECT_TRUE(
        nemo::wal::WALManagerTestAccess::should_wait_for_group_commit(
            threshold - 1));
    EXPECT_FALSE(
        nemo::wal::WALManagerTestAccess::should_wait_for_group_commit(
            threshold));
    EXPECT_FALSE(
        nemo::wal::WALManagerTestAccess::should_wait_for_group_commit(
            threshold + 1));
}

TEST(WALManagerTest, RollsOverAtThe256MegabyteSegmentLimit) {
    TemporaryDirectory directory;
    const auto payload_size =
        nemo::wal::WALManager::BUFFER_CAPACITY - nemo::wal::LogRecord::HEADER_SIZE;
    const auto record_count =
        nemo::wal::WALManager::MAX_SEGMENT_SIZE /
        nemo::wal::WALManager::BUFFER_CAPACITY;

    {
        nemo::wal::WALManager manager(
            directory.path().string(), nemo::wal::DurabilityMode::Full);
        for (std::uint64_t txn_id = 1; txn_id <= record_count; ++txn_id) {
            nemo::wal::LogRecord record(
                txn_id, nemo::wal::LogRecordType::TxnBegin,
                nemo::wal::LSN::invalid(),
                std::vector<std::uint8_t>(payload_size, 0));
            ASSERT_EQ(record.get_size(), nemo::wal::WALManager::BUFFER_CAPACITY);
            const auto lsn = manager.append(record);
            manager.flush_up_to(lsn);
        }
    }

    const auto first_segment = directory.path() / "0000000000000001.wal";
    const auto second_segment = directory.path() / "0000000000000002.wal";
    ASSERT_TRUE(std::filesystem::exists(first_segment));
    ASSERT_TRUE(std::filesystem::exists(second_segment));
    EXPECT_EQ(
        std::filesystem::file_size(first_segment),
        nemo::wal::WALSegment::HEADER_SIZE +
            (record_count - 1) * nemo::wal::WALManager::BUFFER_CAPACITY);
    EXPECT_EQ(
        std::filesystem::file_size(second_segment),
        nemo::wal::WALSegment::HEADER_SIZE +
            nemo::wal::WALManager::BUFFER_CAPACITY);
    EXPECT_LE(
        std::filesystem::file_size(first_segment),
        nemo::wal::WALManager::MAX_SEGMENT_SIZE);
    EXPECT_GT(
        std::filesystem::file_size(first_segment) +
            nemo::wal::WALManager::BUFFER_CAPACITY,
        nemo::wal::WALManager::MAX_SEGMENT_SIZE);

    nemo::wal::WALManager reopened(
        directory.path().string(), nemo::wal::DurabilityMode::Full);
    auto next_record = make_record(record_count + 1);
    const auto next_lsn = reopened.append(next_record);
    EXPECT_EQ(next_lsn.value(), record_count + 1);
    reopened.flush_up_to(next_lsn);
}

TEST(WALManagerTest, SurfacesAndLatchesInjectedWriteFailure) {
    TemporaryDirectory directory;
    nemo::wal::WALManager manager(
        directory.path().string(), nemo::wal::DurabilityMode::Full);
    auto record = make_record(1);
    const auto lsn = manager.append(record);
    nemo::wal::WALManagerTestAccess::inject_write_failure(manager);

    EXPECT_THROW(manager.flush_up_to(lsn), std::runtime_error);
    EXPECT_EQ(manager.get_flushed_lsn().value(), 0);
    EXPECT_THROW(manager.flush(), std::runtime_error);
}

TEST(WALManagerTest, SurfacesAndLatchesInjectedFsyncFailure) {
    TemporaryDirectory directory;
    nemo::wal::WALManager manager(
        directory.path().string(), nemo::wal::DurabilityMode::Full);
    auto record = make_record(1);
    const auto lsn = manager.append(record);
    nemo::wal::WALManagerTestAccess::inject_fsync_failure(manager);

    EXPECT_THROW(manager.flush_up_to(lsn), std::runtime_error);
    EXPECT_EQ(manager.get_flushed_lsn().value(), 0);
    EXPECT_THROW(manager.flush(), std::runtime_error);
}

TEST(WALManagerTest, RejectsARecordWithAnInvalidChecksumOnReopen) {
    TemporaryDirectory directory;
    {
        nemo::wal::WALManager manager(
            directory.path().string(), nemo::wal::DurabilityMode::Full);
        auto record = make_record(1);
        const auto lsn = manager.append(record);
        manager.flush_up_to(lsn);
    }

    const auto segment_path = directory.path() / "0000000000000001.wal";
    std::fstream segment(segment_path, std::ios::in | std::ios::out | std::ios::binary);
    ASSERT_TRUE(segment.is_open());
    const auto checksum_offset = static_cast<std::streamoff>(
        nemo::wal::WALSegment::HEADER_SIZE + 37);
    segment.seekg(checksum_offset);
    char checksum_byte = 0;
    segment.read(&checksum_byte, 1);
    ASSERT_TRUE(segment.good());
    checksum_byte ^= 0x01;
    segment.seekp(checksum_offset);
    segment.write(&checksum_byte, 1);
    segment.close();

    EXPECT_THROW(
        nemo::wal::WALManager(
            directory.path().string(), nemo::wal::DurabilityMode::Full),
        std::runtime_error);
}

