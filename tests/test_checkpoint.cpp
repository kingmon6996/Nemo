#include "nemo/core/config.hpp"
#include "nemo/storage/buffer_pool.hpp"
#include "nemo/storage/disk_manager.hpp"
#include "nemo/transaction/transaction_manager.hpp"
#include "nemo/wal/checkpoint_manager.hpp"
#include "nemo/wal/log_reader.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <string>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
            ("nemo_checkpoint_test_" + std::to_string(nonce));
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

} // namespace

TEST(CheckpointManagerTest, CheckpointPersistsStateAndTruncatesSafeSegments) {
    TemporaryDirectory directory;
    const auto data_path = directory.path() / "data.nm";
    const auto wal_path = directory.path() / "wal";
    std::uint64_t checkpoint_lsn = 0;
    std::uint64_t expected_next_lsn = 0;
    const std::uint64_t committed_transaction_count = 8;
    const std::uint64_t active_transaction_id = committed_transaction_count + 1;

    {
        nemo::Config config;
        config.set_page_size(8192);
        config.set_buffer_pool_size(2);
        auto disk_result = nemo::storage::DiskManager::create(data_path, config);
        ASSERT_TRUE(disk_result);
        auto disk = std::move(disk_result.value());
        nemo::wal::WALManager wal(
            wal_path.string(), nemo::wal::DurabilityMode::Full, 128);
        disk->set_wal_manager(&wal);
        nemo::storage::BufferPool pool(2, disk.get(), &wal);
        nemo::transaction::TransactionManager transactions(nullptr, &wal);

        for (std::uint64_t txn_id = 1;
             txn_id <= committed_transaction_count; ++txn_id) {
            nemo::wal::LogRecord begin(
                txn_id, nemo::wal::LogRecordType::TxnBegin,
                nemo::wal::LSN::invalid());
            const auto begin_lsn = wal.append(begin);
            wal.flush_up_to(begin_lsn);
            nemo::wal::LogRecord commit(
                txn_id, nemo::wal::LogRecordType::TxnCommit, begin_lsn);
            const auto commit_lsn = wal.append(commit);
            wal.flush_up_to(commit_lsn);
        }
        nemo::wal::LogRecord active_begin(
            active_transaction_id, nemo::wal::LogRecordType::TxnBegin,
            nemo::wal::LSN::invalid());
        const auto active_begin_lsn = wal.append(active_begin);
        wal.flush_up_to(active_begin_lsn);

        std::size_t old_segment_count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(wal_path)) {
            if (entry.path().extension() == ".wal") ++old_segment_count;
        }
        ASSERT_GT(old_segment_count, 2U);

        nemo::wal::CheckpointManager checkpoint(
            &wal, &transactions, &pool,
            (directory.path() / "master.nm").string(), wal_path.string());
        ASSERT_TRUE(checkpoint.perform_checkpoint());
        const auto master = checkpoint.read_master_record();
        ASSERT_TRUE(master.has_value());
        checkpoint_lsn = master->checkpoint_lsn.value();
        const auto state = checkpoint.read_checkpoint_state(master->checkpoint_lsn);
        ASSERT_TRUE(state.has_value());
        EXPECT_EQ(state->max_transaction_id, active_transaction_id);
        ASSERT_EQ(state->transactions.size(), 1U);
        EXPECT_EQ(
            state->transactions.front().transaction_id, active_transaction_id);
        EXPECT_EQ(state->transactions.front().first_lsn, active_begin_lsn.value());
        EXPECT_EQ(state->transactions.front().last_lsn, active_begin_lsn.value());
        EXPECT_TRUE(state->dirty_pages.empty());

        std::size_t retained_segment_count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(wal_path)) {
            if (entry.path().extension() == ".wal") ++retained_segment_count;
        }
        EXPECT_GE(retained_segment_count, 1U);
        EXPECT_LT(retained_segment_count, old_segment_count);

        nemo::wal::LogReader reader(wal_path.string());
        ASSERT_TRUE(reader.initialize());
        ASSERT_TRUE(reader.seek_to_lsn(master->checkpoint_lsn));
        const auto begin = reader.read_next();
        ASSERT_TRUE(begin.has_value());
        EXPECT_EQ(begin->get_type(), nemo::wal::LogRecordType::CheckpointBegin);
        EXPECT_EQ(begin->get_lsn().value(), checkpoint_lsn);
        ASSERT_TRUE(disk->close());
        expected_next_lsn = wal.get_flushed_lsn().value() + 1;
    }

    {
        nemo::wal::WALManager reopened(
            wal_path.string(), nemo::wal::DurabilityMode::Full, 128);
        EXPECT_EQ(
            reopened.max_transaction_id(), active_transaction_id);
        const auto active_transactions = reopened.active_transactions();
        ASSERT_EQ(active_transactions.size(), 1U);
        EXPECT_EQ(
            active_transactions.front().transaction_id, active_transaction_id);
        auto next = nemo::wal::LogRecord(
            active_transaction_id + 1,
            nemo::wal::LogRecordType::TxnBegin,
            nemo::wal::LSN::invalid());
        EXPECT_EQ(reopened.append(next).value(), expected_next_lsn);
        reopened.flush_up_to(nemo::wal::LSN(expected_next_lsn));
    }
}
