// Copyright 2026 Pravanjan Roy (kingmon6996)
// SPDX-License-Identifier: Apache-2.0

#include "nemo/catalog/catalog_serializer.hpp"
#include "nemo/catalog/column.hpp"
#include "nemo/core/config.hpp"
#include "nemo/storage/buffer_pool.hpp"
#include "nemo/storage/disk_manager.hpp"
#include "nemo/storage/page_handle.hpp"
#include "nemo/types/type_serializer.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace {

std::filesystem::path make_temp_database_path() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("nemo_catalog_test_" + std::to_string(nonce) + ".nm");
}

struct TempDatabaseCleanup {
    std::filesystem::path path;

    ~TempDatabaseCleanup() {
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(path.string() + ".lock", error);
    }
};

TEST(CatalogSerializerTest, RoundTripsMultiPageCatalog) {
    const auto path = make_temp_database_path();
    [[maybe_unused]] const TempDatabaseCleanup cleanup{path};
    nemo::Config config;
    config.set_page_size(8192);
    config.set_buffer_pool_size(32);

    std::unordered_map<std::uint32_t, nemo::catalog::SchemaMetadata> schemas;
    std::unordered_map<std::uint32_t, nemo::catalog::TableMetadata> tables;
    std::unordered_map<std::uint32_t, nemo::catalog::IndexMetadata> indexes;

    nemo::catalog::SchemaMetadata schema;
    schema.schema_id = nemo::catalog::SchemaID(1);
    schema.name = "main";
    schemas.emplace(1, schema);

    for (std::uint32_t i = 1; i <= 160; ++i) {
        nemo::catalog::Column id_column(
            nemo::catalog::ColumnID(i * 2), "id",
            nemo::types::TypeID::Integer, 0, false);
        id_column.set_default_value(
            nemo::catalog::DefaultValue(nemo::types::Value(static_cast<std::int32_t>(i))));

        nemo::catalog::Column text_column(
            nemo::catalog::ColumnID(i * 2 + 1), "label",
            nemo::types::TypeID::Text, 1, true);

        nemo::catalog::TableMetadata table;
        table.table_id = nemo::TableID(i);
        table.schema_id = nemo::catalog::SchemaID(1);
        table.name = "table_" + std::to_string(i);
        table.version = 3;
        table.first_page_id = nemo::PageID(i + 100);
        table.schema.add_column(std::move(id_column));
        table.schema.add_column(std::move(text_column));
        tables.emplace(i, std::move(table));
    }

    nemo::catalog::IndexMetadata index;
    index.index_id = nemo::IndexID(1);
    index.schema_id = nemo::catalog::SchemaID(1);
    index.table_id = nemo::TableID(1);
    index.name = "idx_table_1_id";
    index.indexed_columns.emplace_back(2);
    index.root_page_id = nemo::PageID(500);
    index.tree_height = 2;
    index.is_unique = true;
    indexes.emplace(1, index);

    std::uint32_t next_schema = 2;
    std::uint32_t next_table = 161;
    std::uint32_t next_column = 321;
    std::uint32_t next_index = 2;
    std::uint32_t next_constraint = 1;
    nemo::PageID root_page;

    {
        auto disk_result = nemo::storage::DiskManager::create(path, config);
        ASSERT_TRUE(disk_result) << nemo::to_string(disk_result.error());
        auto disk_manager = std::move(disk_result.value());
        nemo::storage::BufferPool buffer_pool(32, disk_manager.get(), nullptr);
        auto root_result = buffer_pool.allocate_page();
        ASSERT_TRUE(root_result);
        root_page = root_result.value().get().id();

        auto save_result = nemo::catalog::CatalogSerializer::save(
            &buffer_pool, root_page, schemas, tables, indexes,
            next_schema, next_table, next_column, next_index, next_constraint);
        ASSERT_TRUE(save_result);
        ASSERT_TRUE(buffer_pool.flush_all());
    }

    {
        auto disk_result = nemo::storage::DiskManager::open(path, config);
        ASSERT_TRUE(disk_result) << nemo::to_string(disk_result.error());
        auto disk_manager = std::move(disk_result.value());
        nemo::storage::BufferPool buffer_pool(32, disk_manager.get(), nullptr);

        std::unordered_map<std::uint32_t, nemo::catalog::SchemaMetadata> loaded_schemas;
        std::unordered_map<std::uint32_t, nemo::catalog::TableMetadata> loaded_tables;
        std::unordered_map<std::uint32_t, nemo::catalog::IndexMetadata> loaded_indexes;
        std::uint32_t loaded_next_schema = 0;
        std::uint32_t loaded_next_table = 0;
        std::uint32_t loaded_next_column = 0;
        std::uint32_t loaded_next_index = 0;
        std::uint32_t loaded_next_constraint = 0;

        auto load_result = nemo::catalog::CatalogSerializer::load(
            &buffer_pool, root_page, loaded_schemas, loaded_tables, loaded_indexes,
            loaded_next_schema, loaded_next_table, loaded_next_column,
            loaded_next_index, loaded_next_constraint);
        ASSERT_TRUE(load_result);
        EXPECT_EQ(loaded_schemas.size(), 1);
        EXPECT_EQ(loaded_tables.size(), tables.size());
        EXPECT_EQ(loaded_indexes.size(), 1);
        EXPECT_EQ(loaded_schemas.at(1).name, "main");
        EXPECT_EQ(loaded_tables.at(160).name, "table_160");
        EXPECT_EQ(loaded_tables.at(1).first_page_id, nemo::PageID(101));
        EXPECT_EQ(loaded_tables.at(1).schema.column_count(), 2);
        EXPECT_EQ(loaded_tables.at(1).schema.column_by_ordinal(0).name(), "id");
        EXPECT_EQ(loaded_tables.at(1).schema.column_by_ordinal(0).type(),
                  nemo::types::TypeID::Integer);
        ASSERT_TRUE(loaded_tables.at(1).schema.column_by_ordinal(0)
                        .default_value().has_value());
        EXPECT_EQ(loaded_tables.at(1).schema.column_by_ordinal(0)
                      .default_value().value().to_string(), "1");
        EXPECT_EQ(loaded_indexes.at(1).root_page_id, nemo::PageID(500));
        EXPECT_TRUE(loaded_indexes.at(1).is_unique);
        EXPECT_EQ(loaded_next_schema, next_schema);
        EXPECT_EQ(loaded_next_table, next_table);
        EXPECT_EQ(loaded_next_column, next_column);
        EXPECT_EQ(loaded_next_index, next_index);
        EXPECT_EQ(loaded_next_constraint, next_constraint);
    }

}

} // namespace