// Copyright 2026 Pravanjan Roy (kingmon6996)
// SPDX-License-Identifier: Apache-2.0

#include "nemo/api/connection.hpp"
#include "nemo/api/result.hpp"
#include "nemo/api/row.hpp"
#include "nemo/api/database.hpp"
#include "nemo/core/config.hpp"
#include "nemo/storage/disk_manager.hpp"
#include "nemo/storage/page_header.hpp"
#include "nemo/storage/storage_constants.hpp"
#include "nemo/types/type_id.hpp"
#include "nemo/wal/log_reader.hpp"
#include "nemo/wal/wal_manager.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

std::filesystem::path make_temp_database_path() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("nemo_phase1_database_" + std::to_string(nonce) + ".nm");
}

struct DatabaseFilesCleanup {
    std::filesystem::path path;
    ~DatabaseFilesCleanup() {
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(path.string() + ".legacy.bak", error);
        std::filesystem::remove(path.string() + ".phase1.tmp", error);
        std::filesystem::remove_all(path.string() + ".wal", error);
    }
};

void append_little_endian(std::vector<std::uint8_t>& output,
                          std::uint64_t value, std::size_t byte_count) {
    for (std::size_t index = 0; index < byte_count; ++index) {
        output.push_back(static_cast<std::uint8_t>((value >> (index * 8)) & 0xFF));
    }
}

std::vector<std::uint8_t> make_page_write_payload(
    nemo::PageID page_id, std::span<const std::byte> before,
    std::span<const std::byte> after) {
    std::vector<std::uint8_t> payload;
    payload.reserve(12 + before.size() + after.size());
    append_little_endian(payload, page_id.value(), 8);
    append_little_endian(payload, before.size(), 4);
    for (const auto value : before) {
        payload.push_back(std::to_integer<std::uint8_t>(value));
    }
    for (const auto value : after) {
        payload.push_back(std::to_integer<std::uint8_t>(value));
    }
    return payload;
}

void write_legacy_string(std::ofstream& file, const std::string& value) {
    const auto size = static_cast<std::uint32_t>(value.size());
    file.write(reinterpret_cast<const char*>(&size), sizeof(size));
    file.write(value.data(), static_cast<std::streamsize>(value.size()));
}

void write_legacy_column(std::ofstream& file, const std::string& name,
                         nemo::types::TypeID type, bool nullable) {
    write_legacy_string(file, name);
    const auto raw_type = static_cast<int>(type);
    file.write(reinterpret_cast<const char*>(&raw_type), sizeof(raw_type));
    const std::uint8_t null_flag = nullable ? 1 : 0;
    const std::uint8_t auto_flag = 0;
    file.write(reinterpret_cast<const char*>(&null_flag), sizeof(null_flag));
    file.write(reinterpret_cast<const char*>(&auto_flag), sizeof(auto_flag));
    const std::uint32_t no_sequence = 0;
    file.write(reinterpret_cast<const char*>(&no_sequence), sizeof(no_sequence));
}

void write_legacy_value(std::ofstream& file, nemo::types::TypeID type,
                        const std::string& value) {
    const auto raw_type = static_cast<std::uint16_t>(type);
    const std::uint8_t is_null = 0;
    const auto size = static_cast<std::uint32_t>(value.size());
    file.write(reinterpret_cast<const char*>(&raw_type), sizeof(raw_type));
    file.write(reinterpret_cast<const char*>(&is_null), sizeof(is_null));
    file.write(reinterpret_cast<const char*>(&size), sizeof(size));
    file.write(value.data(), static_cast<std::streamsize>(value.size()));
}

TEST(DatabaseStorageTest, MigratesLegacySnapshotAndReopensPageDatabase) {
    const auto path = make_temp_database_path();
    [[maybe_unused]] const DatabaseFilesCleanup cleanup{path};
    {
        std::ofstream legacy(path, std::ios::binary);
        ASSERT_TRUE(legacy.is_open());
        legacy.write("NEMOMETA", 8);
        const std::uint32_t no_sequences = 0;
        legacy.write(reinterpret_cast<const char*>(&no_sequences), sizeof(no_sequences));
        const std::uint32_t table_count = 1;
        legacy.write(reinterpret_cast<const char*>(&table_count), sizeof(table_count));
        write_legacy_string(legacy, "legacy_values");
        const std::uint64_t row_count = 1;
        const std::uint64_t no_autoincrement = static_cast<std::uint64_t>(-1);
        legacy.write(reinterpret_cast<const char*>(&row_count), sizeof(row_count));
        legacy.write(reinterpret_cast<const char*>(&no_autoincrement), sizeof(no_autoincrement));
        const std::uint32_t no_sequence_name = 0;
        legacy.write(reinterpret_cast<const char*>(&no_sequence_name), sizeof(no_sequence_name));
        using nemo::types::TypeID;
        const std::vector<std::pair<std::string, TypeID>> columns = {
            {"id", TypeID::Integer}, {"label", TypeID::Text},
            {"active", TypeID::Boolean}, {"small_value", TypeID::SmallInt},
            {"large_value", TypeID::BigInt}, {"real_value", TypeID::Real},
            {"double_value", TypeID::Double}, {"char_value", TypeID::Char},
            {"varchar_value", TypeID::VarChar}, {"uuid_value", TypeID::UUID},
            {"json_value", TypeID::JSON}, {"jsonb_value", TypeID::JSONB},
            {"bit_value", TypeID::Bit},
            {"varbit_value", TypeID::VarBit}, {"inet_value", TypeID::Inet},
            {"cidr_value", TypeID::Cidr}, {"macaddr_value", TypeID::MacAddr},
            {"range_value", TypeID::Range}, {"numeric_value", TypeID::Numeric},
            {"bytea_value", TypeID::Bytea}, {"date_value", TypeID::Date},
            {"time_value", TypeID::Time}, {"timestamp_value", TypeID::Timestamp},
            {"timestamptz_value", TypeID::TimestampTZ},
            {"interval_value", TypeID::Interval}};
        const auto column_count = static_cast<std::uint32_t>(columns.size());
        legacy.write(reinterpret_cast<const char*>(&column_count), sizeof(column_count));
        for (const auto& [name, type] : columns) {
            write_legacy_column(legacy, name, type, name != "id");
        }
        const std::uint32_t sample_row_count = 1;
        const auto row_column_count = static_cast<std::uint32_t>(columns.size());
        legacy.write(reinterpret_cast<const char*>(&sample_row_count), sizeof(sample_row_count));
        legacy.write(reinterpret_cast<const char*>(&row_column_count), sizeof(row_column_count));
        const std::vector<std::string> values = {
            "41", "preserved", "true", "-12", "123456789012", "1.25",
            "9.5", "c", "varchar", "550e8400-e29b-41d4-a716-446655440000",
            "{\"json\":true}", "{\"jsonb\":true}", "101", "10101", "127.0.0.1",
            "127.0.0.0/8", "00:11:22:33:44:55", "[1,5]", "12.34",
            "\\x0001ff", "2000-01-24", "00:02:03.123456",
            "TIMESTAMP(654321 us)", "TIMESTAMP(987654 us)",
            "2 months 3 days 00:00:04.000004"};
        ASSERT_EQ(values.size(), columns.size());
        for (std::size_t i = 0; i < columns.size(); ++i) {
            write_legacy_value(legacy, columns[i].second, values[i]);
        }
        ASSERT_TRUE(legacy.good());
    }

    nemo::DatabaseConfig config;
    config.buffer_pool_size = 32 * 1024 * 1024;
    auto opened = nemo::Database::open(path.string(), config);
    ASSERT_TRUE(opened) << nemo::to_string(opened.error());
    auto database = std::move(opened.value());
    auto connected = database.connect({});
    ASSERT_TRUE(connected);
    auto connection = std::move(connected.value());
    auto create_result = connection.execute(
        "CREATE TABLE phase1_values (id INTEGER, label TEXT)");
    ASSERT_TRUE(create_result);
    auto insert_result = connection.execute(
        "INSERT INTO phase1_values VALUES (7, 'page-format')");
    ASSERT_TRUE(insert_result) << nemo::to_string(insert_result.error());
    ASSERT_TRUE(connection.execute(
        "INSERT INTO phase1_values VALUES (8, 'delete-me')"));
    auto update_result = connection.execute(
        "UPDATE phase1_values SET label = 'updated-page-format' WHERE id = 7");
    ASSERT_TRUE(update_result) << nemo::to_string(update_result.error());
    EXPECT_EQ(update_result.value().affected_rows(), 1);
    auto delete_result = connection.execute(
        "DELETE FROM phase1_values WHERE id = 8");
    ASSERT_TRUE(delete_result) << nemo::to_string(delete_result.error());
    EXPECT_EQ(delete_result.value().affected_rows(), 1);

    auto committed_transaction = connection.begin();
    ASSERT_TRUE(committed_transaction);
    auto transactional_insert = connection.execute(
        "INSERT INTO phase1_values VALUES (9, 'committed')");
    ASSERT_TRUE(transactional_insert) << nemo::to_string(transactional_insert.error());
    ASSERT_TRUE(committed_transaction.value().commit());

    auto rolled_back_transaction = connection.begin();
    ASSERT_TRUE(rolled_back_transaction);
    auto rolled_back_insert = connection.execute(
        "INSERT INTO phase1_values VALUES (10, 'rolled-back')");
    ASSERT_TRUE(rolled_back_insert) << nemo::to_string(rolled_back_insert.error());
    ASSERT_TRUE(rolled_back_transaction.value().rollback());

    auto savepoint_transaction = connection.begin();
    ASSERT_TRUE(savepoint_transaction);
    ASSERT_TRUE(connection.execute(
        "INSERT INTO phase1_values VALUES (11, 'before-savepoint')"));
    ASSERT_TRUE(connection.execute("SAVEPOINT phase1_checkpoint"));
    ASSERT_TRUE(connection.execute(
        "INSERT INTO phase1_values VALUES (12, 'after-savepoint')"));
    ASSERT_TRUE(connection.execute("ROLLBACK TO SAVEPOINT phase1_checkpoint"));
    ASSERT_TRUE(savepoint_transaction.value().commit());

    ASSERT_TRUE(connection.execute(
        "CREATE TABLE phase1_ctas AS SELECT id, label FROM phase1_values"));
    auto ctas_rows = connection.execute("SELECT id, label FROM phase1_ctas");
    ASSERT_TRUE(ctas_rows) << nemo::to_string(ctas_rows.error());
    std::size_t ctas_count = 0;
    bool found_savepoint_row = false;
    while (auto row = ctas_rows.value().next()) {
        ++ctas_count;
        EXPECT_NE(row->get(0).to_string(), "12");
        if (row->get(0).to_string() == "11") {
            EXPECT_EQ(row->get(1).to_string(), "before-savepoint");
            found_savepoint_row = true;
        }
    }
    EXPECT_EQ(ctas_count, 3);
    EXPECT_TRUE(found_savepoint_row);
    auto drop_column = connection.execute(
        "ALTER TABLE phase1_ctas DROP COLUMN label");
    ASSERT_TRUE(drop_column) << nemo::to_string(drop_column.error());
    auto dropped_column_rows = connection.execute("SELECT id FROM phase1_ctas");
    ASSERT_TRUE(dropped_column_rows) << nemo::to_string(dropped_column_rows.error());
    std::size_t dropped_column_count = 0;
    while (auto row = dropped_column_rows.value().next()) {
        ++dropped_column_count;
        EXPECT_EQ(row->size(), 1);
    }
    EXPECT_EQ(dropped_column_count, 3);

    auto add_column = connection.execute(
        "ALTER TABLE phase1_values ADD COLUMN note TEXT DEFAULT 'altered'");
    ASSERT_TRUE(add_column) << nemo::to_string(add_column.error());
    auto altered_rows = connection.execute("SELECT id, note FROM phase1_values");
    ASSERT_TRUE(altered_rows) << nemo::to_string(altered_rows.error());
    while (auto row = altered_rows.value().next()) {
        EXPECT_EQ(row->get(1).to_string(), "altered");
    }

    auto live_rows = connection.execute("SELECT id, label FROM phase1_values");
    ASSERT_TRUE(live_rows) << nemo::to_string(live_rows.error());
    ASSERT_TRUE(live_rows.value().has_rows());
    auto live_row = live_rows.value().next();
    ASSERT_NE(live_row, nullptr);
    EXPECT_EQ(live_row->get(0).to_string(), "7");
    EXPECT_EQ(live_row->get(1).to_string(), "updated-page-format");
    auto committed_row = live_rows.value().next();
    ASSERT_NE(committed_row, nullptr);
    EXPECT_EQ(committed_row->get(0).to_string(), "9");
    EXPECT_EQ(committed_row->get(1).to_string(), "committed");
    auto savepoint_row = live_rows.value().next();
    ASSERT_NE(savepoint_row, nullptr);
    EXPECT_EQ(savepoint_row->get(0).to_string(), "11");
    EXPECT_EQ(savepoint_row->get(1).to_string(), "before-savepoint");
    EXPECT_FALSE(live_rows.value().next());

    ASSERT_TRUE(connection.execute(
        "CREATE TABLE bulk_values (id INTEGER, payload TEXT)"));
    const std::string payload(200, 'x');
    for (std::int64_t id = 100; id < 145; ++id) {
        const auto insert = connection.execute(
            "INSERT INTO bulk_values VALUES (" + std::to_string(id) +
            ", '" + payload + "')");
        ASSERT_TRUE(insert) << nemo::to_string(insert.error());
    }
    const std::string expanded_payload(4000, 'y');
    auto expand_row = connection.execute(
        "UPDATE bulk_values SET payload = '" + expanded_payload +
        "' WHERE id = 100");
    ASSERT_TRUE(expand_row) << nemo::to_string(expand_row.error());
    auto remove_row = connection.execute(
        "DELETE FROM bulk_values WHERE id = 101");
    ASSERT_TRUE(remove_row) << nemo::to_string(remove_row.error());
    EXPECT_EQ(remove_row.value().affected_rows(), 1);
    auto bulk_rows = connection.execute(
        "SELECT id, payload FROM bulk_values");
    ASSERT_TRUE(bulk_rows) << nemo::to_string(bulk_rows.error());
    std::size_t bulk_count = 0;
    bool found_expanded_row = false;
    while (auto row = bulk_rows.value().next()) {
        ++bulk_count;
        if (row->get(0).to_string() == "100") {
            EXPECT_EQ(row->get(1).to_string().size(), expanded_payload.size());
            found_expanded_row = true;
        }
        EXPECT_NE(row->get(0).to_string(), "101");
    }
    EXPECT_EQ(bulk_count, 44);
    EXPECT_TRUE(found_expanded_row);

    const auto live_stats = database.statistics();
    EXPECT_GT(live_stats.buffer_hits, 0);
    EXPECT_GT(live_stats.buffer_misses, 0);

    const auto flush_result = database.flush();
    ASSERT_TRUE(flush_result) << nemo::to_string(flush_result.error());

    std::ifstream page_file(path, std::ios::binary);
    ASSERT_TRUE(page_file.is_open());
    char magic[8]{};
    page_file.read(magic, sizeof(magic));
    ASSERT_EQ(page_file.gcount(), static_cast<std::streamsize>(sizeof(magic)));
    EXPECT_TRUE(std::equal(std::begin(magic), std::end(magic),
                           nemo::storage::MAGIC_BYTES.begin()));
    EXPECT_TRUE(std::filesystem::exists(path.string() + ".legacy.bak"));
    ASSERT_TRUE(database.close());

    nemo::Config storage_config;
    storage_config.set_page_size(8192);
    storage_config.set_buffer_pool_size(32);
    auto storage_result = nemo::storage::DiskManager::open(path, storage_config);
    ASSERT_TRUE(storage_result);
    auto disk_manager = std::move(storage_result.value());
    const auto& header = disk_manager->get_database_header();
    EXPECT_NE(header.catalog_root_page.value(), 0);
    EXPECT_NE(header.metadata_root_page.value(), 0);
    bool found_table_page = false;
    for (std::uint64_t page_id = 1; page_id < header.next_page_id.value(); ++page_id) {
        auto page = disk_manager->read_page(nemo::PageID(page_id));
        ASSERT_TRUE(page) << nemo::to_string(page.error());
        if (page.value().read_header().page_type == nemo::storage::PageType::Table) {
            found_table_page = true;
            break;
        }
    }
    EXPECT_TRUE(found_table_page);
    ASSERT_TRUE(disk_manager->close());

    auto reopened = nemo::Database::open(path.string(), config);
    ASSERT_TRUE(reopened) << nemo::to_string(reopened.error());
    auto reopened_database = std::move(reopened.value());
    const auto buffer_stats = reopened_database.statistics();
    EXPECT_GT(buffer_stats.buffer_misses, 0);
    EXPECT_GT(buffer_stats.buffer_hits, 0);
    EXPECT_EQ(buffer_stats.buffer_pool_frames, (32 * 1024 * 1024) / 8192);
    auto reopened_connection_result = reopened_database.connect({});
    ASSERT_TRUE(reopened_connection_result);
    auto reopened_connection = std::move(reopened_connection_result.value());
    auto legacy_select = reopened_connection.execute(
        "SELECT * FROM legacy_values");
    ASSERT_TRUE(legacy_select) << nemo::to_string(legacy_select.error());
    ASSERT_TRUE(legacy_select.value().has_rows());
    const auto legacy_columns = legacy_select.value().columns();
    const std::vector<nemo::types::TypeID> expected_legacy_types = {
        nemo::types::TypeID::Integer, nemo::types::TypeID::Text,
        nemo::types::TypeID::Boolean, nemo::types::TypeID::SmallInt,
        nemo::types::TypeID::BigInt, nemo::types::TypeID::Real,
        nemo::types::TypeID::Double, nemo::types::TypeID::Char,
        nemo::types::TypeID::VarChar, nemo::types::TypeID::UUID,
        nemo::types::TypeID::JSON, nemo::types::TypeID::JSONB,
        nemo::types::TypeID::Bit,
        nemo::types::TypeID::VarBit, nemo::types::TypeID::Inet,
        nemo::types::TypeID::Cidr, nemo::types::TypeID::MacAddr,
        nemo::types::TypeID::Range, nemo::types::TypeID::Numeric,
        nemo::types::TypeID::Bytea, nemo::types::TypeID::Date,
        nemo::types::TypeID::Time, nemo::types::TypeID::Timestamp,
        nemo::types::TypeID::TimestampTZ, nemo::types::TypeID::Interval};
    ASSERT_EQ(legacy_columns.size(), expected_legacy_types.size());
    for (std::size_t i = 0; i < expected_legacy_types.size(); ++i) {
        EXPECT_EQ(legacy_columns[i].type, static_cast<int>(expected_legacy_types[i]));
    }
    auto legacy_row = legacy_select.value().next();
    ASSERT_NE(legacy_row, nullptr);
    const std::vector<std::string> expected_legacy_values = {
        "41", "preserved", "true", "-12", "123456789012", "1.250000",
        "9.500000", "c", "varchar", "550e8400-e29b-41d4-a716-446655440000",
        R"json("{"json":true}")json", "{\"jsonb\":true}", "101", "10101",
        "127.0.0.1",
        "127.0.0.0/8", "00:11:22:33:44:55", "[1,5]", "12.34",
        "\\x0001ff", "2000-01-24", "00:02:03.123456",
        "TIMESTAMP(654321 us)", "TIMESTAMP(987654 us)",
        "2 months 3 days 00:00:04.000004"};
    for (std::size_t i = 0; i < expected_legacy_values.size(); ++i) {
        EXPECT_EQ(legacy_row->get(i).to_string(), expected_legacy_values[i])
            << "Column " << legacy_columns[i].name;
    }
    EXPECT_FALSE(legacy_select.value().next());

    auto selected = reopened_connection.execute(
        "SELECT id, label FROM phase1_values");
    ASSERT_TRUE(selected);
    ASSERT_TRUE(selected.value().has_rows());
    auto row = selected.value().next();
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->get(0).to_string(), "7");
    EXPECT_EQ(row->get(1).to_string(), "updated-page-format");
    auto persisted_commit = selected.value().next();
    ASSERT_NE(persisted_commit, nullptr);
    EXPECT_EQ(persisted_commit->get(0).to_string(), "9");
    EXPECT_EQ(persisted_commit->get(1).to_string(), "committed");
    auto persisted_savepoint = selected.value().next();
    ASSERT_NE(persisted_savepoint, nullptr);
    EXPECT_EQ(persisted_savepoint->get(0).to_string(), "11");
    EXPECT_EQ(persisted_savepoint->get(1).to_string(), "before-savepoint");
    EXPECT_FALSE(selected.value().next());
    auto persisted_alter = reopened_connection.execute(
        "SELECT id, note FROM phase1_values");
    ASSERT_TRUE(persisted_alter) << nemo::to_string(persisted_alter.error());
    std::size_t altered_count = 0;
    while (auto altered_row = persisted_alter.value().next()) {
        ++altered_count;
        EXPECT_EQ(altered_row->get(1).to_string(), "altered");
    }
    EXPECT_EQ(altered_count, 3);

    const auto misses_before_cold_point = reopened_database.statistics().buffer_misses;
    auto cold_point = reopened_connection.execute(
        "SELECT * FROM bulk_values WHERE id = 100");
    ASSERT_TRUE(cold_point) << nemo::to_string(cold_point.error());
    ASSERT_TRUE(cold_point.value().has_rows());
    auto cold_point_row = cold_point.value().next();
    ASSERT_NE(cold_point_row, nullptr);
    EXPECT_EQ(cold_point_row->get(0).to_string(), "100");
    EXPECT_EQ(cold_point_row->get(1).to_string().size(), 4000);
    const auto misses_after_cold_point = reopened_database.statistics().buffer_misses;
    EXPECT_LE(misses_after_cold_point - misses_before_cold_point, 1);
    auto repeated_point = reopened_connection.execute(
        "SELECT * FROM bulk_values WHERE id = 100");
    ASSERT_TRUE(repeated_point) << nemo::to_string(repeated_point.error());
    EXPECT_EQ(reopened_database.statistics().buffer_misses, misses_after_cold_point);

    auto reopened_ctas = reopened_connection.execute(
        "SELECT id FROM phase1_ctas");
    ASSERT_TRUE(reopened_ctas) << nemo::to_string(reopened_ctas.error());
    std::size_t reopened_ctas_count = 0;
    while (auto ctas_row = reopened_ctas.value().next()) {
        ++reopened_ctas_count;
        EXPECT_NE(ctas_row->get(0).to_string(), "12");
        EXPECT_EQ(ctas_row->size(), 1);
    }
    EXPECT_EQ(reopened_ctas_count, 3);

    auto reopened_bulk = reopened_connection.execute(
        "SELECT id, payload FROM bulk_values");
    ASSERT_TRUE(reopened_bulk) << nemo::to_string(reopened_bulk.error());
    std::size_t reopened_bulk_count = 0;
    bool reopened_expanded_row = false;
    while (auto bulk_row = reopened_bulk.value().next()) {
        ++reopened_bulk_count;
        if (bulk_row->get(0).to_string() == "100") {
            EXPECT_EQ(bulk_row->get(1).to_string().size(), 4000);
            reopened_expanded_row = true;
        }
        EXPECT_NE(bulk_row->get(0).to_string(), "101");
    }
    EXPECT_EQ(reopened_bulk_count, 44);
    EXPECT_TRUE(reopened_expanded_row);
    ASSERT_TRUE(reopened_database.close());
}

TEST(DatabaseStorageTest, ParsesSQLTypesUsingCanonicalTypeIds) {
    const auto path = make_temp_database_path();
    [[maybe_unused]] const DatabaseFilesCleanup cleanup{path};
    nemo::DatabaseConfig config;
    config.buffer_pool_size = 32 * 1024 * 1024;
    auto opened = nemo::Database::open(path.string(), config);
    ASSERT_TRUE(opened) << nemo::to_string(opened.error());
    auto database = std::move(opened.value());
    auto connected = database.connect({});
    ASSERT_TRUE(connected);
    auto connection = std::move(connected.value());

    auto created = connection.execute(
        "CREATE TABLE sql_types ("
        "bool_col BOOLEAN, smallint_col SMALLINT, int2_col INT2, int_col INTEGER, "
        "bigint_col BIGINT, int8_col INT8, "
        "real_col REAL, double_col DOUBLE, numeric_col NUMERIC, char_col CHAR, "
        "varchar_col VARCHAR(32), character_varying_col CHARACTER VARYING(32), "
        "text_col TEXT, bytea_col BYTEA, date_col DATE, "
        "time_col TIME, timestamp_col TIMESTAMP, timestamptz_col TIMESTAMPTZ, "
        "timestamp_zone_col TIMESTAMP WITH TIME ZONE, "
        "interval_col INTERVAL, uuid_col UUID, json_col JSON, jsonb_col JSONB, "
        "dict_col DICT, array_col ARRAY, enum_col ENUM, bit_col BIT, varbit_col VARBIT, "
        "inet_col INET, cidr_col CIDR, macaddr_col MACADDR, range_col RANGE)");
    ASSERT_TRUE(created) << nemo::to_string(created.error());

    auto selected = connection.execute("SELECT * FROM sql_types");
    ASSERT_TRUE(selected) << nemo::to_string(selected.error());
    const auto columns = selected.value().columns();
    const std::vector<nemo::types::TypeID> expected_types = {
        nemo::types::TypeID::Boolean, nemo::types::TypeID::SmallInt,
        nemo::types::TypeID::SmallInt, nemo::types::TypeID::Integer,
        nemo::types::TypeID::BigInt, nemo::types::TypeID::BigInt,
        nemo::types::TypeID::Real, nemo::types::TypeID::Double,
        nemo::types::TypeID::Numeric, nemo::types::TypeID::Char,
        nemo::types::TypeID::VarChar, nemo::types::TypeID::VarChar,
        nemo::types::TypeID::Text, nemo::types::TypeID::Bytea,
        nemo::types::TypeID::Date, nemo::types::TypeID::Time,
        nemo::types::TypeID::Timestamp, nemo::types::TypeID::TimestampTZ,
        nemo::types::TypeID::TimestampTZ, nemo::types::TypeID::Interval,
        nemo::types::TypeID::UUID, nemo::types::TypeID::JSON,
        nemo::types::TypeID::JSONB, nemo::types::TypeID::DICT,
        nemo::types::TypeID::Array, nemo::types::TypeID::Enum,
        nemo::types::TypeID::Bit, nemo::types::TypeID::VarBit,
        nemo::types::TypeID::Inet, nemo::types::TypeID::Cidr,
        nemo::types::TypeID::MacAddr, nemo::types::TypeID::Range};
    ASSERT_EQ(columns.size(), expected_types.size());
    for (std::size_t i = 0; i < expected_types.size(); ++i) {
        EXPECT_EQ(columns[i].type, static_cast<int>(expected_types[i]))
            << "Column " << columns[i].name;
    }

    ASSERT_TRUE(database.close());
}

TEST(DatabaseStorageTest, RejectsLossyLegacyEnumMigrationWithoutChangingSource) {
    const auto path = make_temp_database_path();
    [[maybe_unused]] const DatabaseFilesCleanup cleanup{path};
    {
        std::ofstream legacy(path, std::ios::binary);
        ASSERT_TRUE(legacy.is_open());
        legacy.write("NEMOMETA", 8);
        const std::uint32_t no_sequences = 0;
        const std::uint32_t table_count = 1;
        legacy.write(reinterpret_cast<const char*>(&no_sequences), sizeof(no_sequences));
        legacy.write(reinterpret_cast<const char*>(&table_count), sizeof(table_count));
        write_legacy_string(legacy, "legacy_enum");
        const std::uint64_t row_count = 1;
        const std::uint64_t no_autoincrement = static_cast<std::uint64_t>(-1);
        legacy.write(reinterpret_cast<const char*>(&row_count), sizeof(row_count));
        legacy.write(reinterpret_cast<const char*>(&no_autoincrement), sizeof(no_autoincrement));
        const std::uint32_t no_sequence_name = 0;
        const std::uint32_t column_count = 1;
        legacy.write(reinterpret_cast<const char*>(&no_sequence_name), sizeof(no_sequence_name));
        legacy.write(reinterpret_cast<const char*>(&column_count), sizeof(column_count));
        write_legacy_column(legacy, "state", nemo::types::TypeID::Enum, false);
        const std::uint32_t sample_row_count = 1;
        const std::uint32_t row_column_count = 1;
        legacy.write(reinterpret_cast<const char*>(&sample_row_count), sizeof(sample_row_count));
        legacy.write(reinterpret_cast<const char*>(&row_column_count), sizeof(row_column_count));
        write_legacy_value(legacy, nemo::types::TypeID::Enum, "active");
        ASSERT_TRUE(legacy.good());
    }

    std::ifstream original_file(path, std::ios::binary);
    const std::vector<char> original((std::istreambuf_iterator<char>(original_file)),
                                     std::istreambuf_iterator<char>());
    nemo::DatabaseConfig config;
    config.buffer_pool_size = 32 * 1024 * 1024;
    auto opened = nemo::Database::open(path.string(), config);
    ASSERT_FALSE(opened);
    EXPECT_EQ(opened.error(), nemo::Status::Corruption);

    std::ifstream preserved_file(path, std::ios::binary);
    const std::vector<char> preserved((std::istreambuf_iterator<char>(preserved_file)),
                                      std::istreambuf_iterator<char>());
    EXPECT_EQ(preserved, original);
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".legacy.bak"));
}

TEST(DatabaseStorageTest, PreservesLegacyFileWhenMigrationValidationFails) {
    const auto path = make_temp_database_path();
    [[maybe_unused]] const DatabaseFilesCleanup cleanup{path};
    {
        std::ofstream legacy(path, std::ios::binary);
        ASSERT_TRUE(legacy.is_open());
        legacy.write("NEMOMETA", 8);
        const std::uint32_t incomplete_sequence_count = 0;
        legacy.write(reinterpret_cast<const char*>(&incomplete_sequence_count),
                     sizeof(incomplete_sequence_count));
        ASSERT_TRUE(legacy.good());
    }

    std::ifstream original_file(path, std::ios::binary);
    const std::vector<char> original((std::istreambuf_iterator<char>(original_file)),
                                     std::istreambuf_iterator<char>());
    ASSERT_FALSE(original.empty());

    nemo::DatabaseConfig config;
    config.buffer_pool_size = 32 * 1024 * 1024;
    auto opened = nemo::Database::open(path.string(), config);
    ASSERT_FALSE(opened);
    EXPECT_EQ(opened.error(), nemo::Status::Corruption);

    std::ifstream preserved_file(path, std::ios::binary);
    const std::vector<char> preserved((std::istreambuf_iterator<char>(preserved_file)),
                                      std::istreambuf_iterator<char>());
    EXPECT_EQ(preserved, original);
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".phase1.tmp"));
}

TEST(DatabaseStorageTest, TransactionCommitIsLoggedAndDurable) {
    const auto path = make_temp_database_path();
    [[maybe_unused]] const DatabaseFilesCleanup cleanup{path};
    nemo::DatabaseConfig config;
    config.buffer_pool_size = 32 * 1024 * 1024;
    auto opened = nemo::Database::open(path.string(), config);
    ASSERT_TRUE(opened) << nemo::to_string(opened.error());
    auto database = std::move(opened.value());
    auto connected = database.connect({});
    ASSERT_TRUE(connected);
    auto connection = std::move(connected.value());

    auto transaction = connection.begin();
    ASSERT_TRUE(transaction);
    ASSERT_TRUE(transaction.value().commit());
    ASSERT_TRUE(database.close());

    nemo::wal::LogReader reader(path.string() + ".wal");
    ASSERT_TRUE(reader.initialize());
    ASSERT_TRUE(reader.seek_to_first());
    const auto begin_record = reader.read_next();
    ASSERT_TRUE(begin_record.has_value());
    std::optional<nemo::wal::LogRecord> previous_transaction_record = begin_record;
    std::optional<nemo::wal::LogRecord> commit_record;
    while (auto record = reader.read_next()) {
        if (record->get_txn_id() != begin_record->get_txn_id()) continue;
        if (record->get_type() == nemo::wal::LogRecordType::TxnCommit) {
            commit_record = std::move(record);
            break;
        }
        previous_transaction_record = std::move(record);
    }
    ASSERT_TRUE(commit_record.has_value());
    EXPECT_EQ(begin_record->get_type(), nemo::wal::LogRecordType::TxnBegin);
    EXPECT_EQ(commit_record->get_type(), nemo::wal::LogRecordType::TxnCommit);
    EXPECT_EQ(begin_record->get_txn_id(), commit_record->get_txn_id());
    ASSERT_TRUE(previous_transaction_record.has_value());
    EXPECT_EQ(previous_transaction_record->get_type(),
              nemo::wal::LogRecordType::PageWrite);
    EXPECT_GT(previous_transaction_record->get_payload_size(), 12);
    EXPECT_EQ(commit_record->get_prev_lsn().value(),
              previous_transaction_record->get_lsn().value());
}

TEST(DatabaseStorageTest, ReplaysCommittedPagesAndUndoesLoserWrites) {
    const auto path = make_temp_database_path();
    [[maybe_unused]] const DatabaseFilesCleanup cleanup{path};
    nemo::DatabaseConfig config;
    config.buffer_pool_size = 32 * 1024 * 1024;
    auto opened = nemo::Database::open(path.string(), config);
    ASSERT_TRUE(opened) << nemo::to_string(opened.error());
    auto database = std::move(opened.value());
    auto connected = database.connect({});
    ASSERT_TRUE(connected);
    auto connection = std::move(connected.value());
    ASSERT_TRUE(connection.execute("CREATE TABLE loser_rows (id INTEGER, value TEXT)"));
    ASSERT_TRUE(connection.execute("INSERT INTO loser_rows VALUES (1, 'preserved')"));
    ASSERT_TRUE(database.close());

    nemo::Config storage_config;
    storage_config.set_page_size(8192);
    storage_config.set_buffer_pool_size(32);
    auto disk_result = nemo::storage::DiskManager::open(path, storage_config);
    ASSERT_TRUE(disk_result);
    auto disk = std::move(disk_result.value());
    const auto page_id = disk->get_database_header().metadata_root_page;
    ASSERT_NE(page_id.value(), 0);
    auto page_result = disk->read_page(page_id);
    ASSERT_TRUE(page_result);
    auto page = std::move(page_result.value());
    auto page_header = page.read_header();
    page_header.lsn = 0;
    page.write_header(page_header);
    ASSERT_TRUE(disk->write_page(page));
    ASSERT_TRUE(disk->close());

    auto after = std::vector<std::byte>(page.data().begin(), page.data().end());
    ASSERT_GT(page.payload_span().size(), 30);
    after[36 + 30] ^= std::byte{0x7F};
    std::uint64_t txn_id = 0;
    {
        nemo::wal::WALManager wal(path.string() + ".wal");
        txn_id = wal.max_transaction_id() + 1;
        nemo::wal::LogRecord begin(txn_id, nemo::wal::LogRecordType::TxnBegin,
                                   nemo::wal::LSN::invalid());
        wal.append(begin);
        auto page_write_payload = make_page_write_payload(page_id, page.data(), after);
        nemo::wal::LogRecord page_write(
            txn_id, nemo::wal::LogRecordType::PageWrite,
            nemo::wal::LSN::invalid(), std::move(page_write_payload));
        wal.append(page_write);
        wal.flush();
    }

    auto recovered = nemo::Database::open(path.string(), config);
    ASSERT_TRUE(recovered) << nemo::to_string(recovered.error());
    auto recovered_database = std::move(recovered.value());
    auto recovered_connection_result = recovered_database.connect({});
    ASSERT_TRUE(recovered_connection_result);
    auto recovered_connection = std::move(recovered_connection_result.value());
    auto rows = recovered_connection.execute("SELECT id, value FROM loser_rows");
    ASSERT_TRUE(rows) << nemo::to_string(rows.error());
    auto row = rows.value().next();
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->get(0).to_string(), "1");
    EXPECT_EQ(row->get(1).to_string(), "preserved");
    ASSERT_TRUE(recovered_database.close());

    nemo::wal::LogReader reader(path.string() + ".wal");
    ASSERT_TRUE(reader.initialize());
    ASSERT_TRUE(reader.seek_to_first());
    bool found_clr = false;
    bool found_end = false;
    while (auto record = reader.read_next()) {
        if (record->get_txn_id() != txn_id) continue;
        found_clr |= record->get_type() == nemo::wal::LogRecordType::CLR;
        found_end |= record->get_type() == nemo::wal::LogRecordType::TxnEnd;
    }
    EXPECT_TRUE(found_clr);
    EXPECT_TRUE(found_end);
}

TEST(DatabaseStorageTest, RefusesToOpenWhenWalSegmentIsCorrupt) {
    const auto path = make_temp_database_path();
    [[maybe_unused]] const DatabaseFilesCleanup cleanup{path};
    nemo::DatabaseConfig config;
    config.buffer_pool_size = 32 * 1024 * 1024;
    auto opened = nemo::Database::open(path.string(), config);
    ASSERT_TRUE(opened) << nemo::to_string(opened.error());
    ASSERT_TRUE(opened.value().close());
    std::ifstream data_before_file(path, std::ios::binary);
    const std::vector<char> data_before(
        (std::istreambuf_iterator<char>(data_before_file)),
        std::istreambuf_iterator<char>());

    const auto wal_segment = std::filesystem::path(path.string() + ".wal") /
        "0000000000000001.wal";
    std::fstream segment(wal_segment, std::ios::in | std::ios::out | std::ios::binary);
    ASSERT_TRUE(segment.is_open());
    const char corrupt_byte = 'X';
    segment.write(&corrupt_byte, 1);
    segment.close();

    auto reopened = nemo::Database::open(path.string(), config);
    ASSERT_FALSE(reopened);
    EXPECT_EQ(reopened.error(), nemo::Status::IOError);
    std::ifstream data_after_file(path, std::ios::binary);
    const std::vector<char> data_after(
        (std::istreambuf_iterator<char>(data_after_file)),
        std::istreambuf_iterator<char>());
    EXPECT_EQ(data_after, data_before);
}

} // namespace