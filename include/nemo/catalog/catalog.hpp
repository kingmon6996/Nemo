#ifndef NEMO_CATALOG_CATALOG_HPP
#define NEMO_CATALOG_CATALOG_HPP

#include "nemo/catalog/catalog_id.hpp"
#include "nemo/catalog/schema.hpp"
#include "nemo/catalog/table_metadata.hpp"
#include "nemo/catalog/index_metadata.hpp"
#include "nemo/storage/buffer_pool.hpp"
#include "nemo/core/status.hpp"
#include "nemo/core/ids.hpp"
#include <unordered_map>
#include <string>
#include <shared_mutex>
#include <memory>
#include <vector>

namespace nemo::catalog {

class Catalog {
public:
    explicit Catalog(storage::BufferPool* buffer_pool);

    // Initialize an empty catalog, set up default "main" schema, and persist.
    Result<void> init_new(PageID* out_catalog_root);

    // Load an existing catalog from the root page.
    Result<void> load(PageID catalog_root);
    
    // Validate structural integrity of the catalog caches.
    Result<void> validate() const;

    // --- Schema Operations ---
    Result<SchemaID> create_schema(const std::string& name);
    Result<void> drop_schema(SchemaID schema_id);
    Result<SchemaMetadata> find_schema(const std::string& name) const;

    // --- Table Operations ---
    Result<TableID> create_table(SchemaID schema_id, const std::string& table_name, const TableSchema& schema);
    Result<void> drop_table(TableID table_id);
    
    Result<TableMetadata> find_table(TableID table_id) const;
    Result<TableMetadata> find_table(SchemaID schema_id, const std::string& table_name) const;

    // --- Index Operations ---
    Result<IndexID> create_index(TableID table_id, const std::string& index_name, 
                                 const std::vector<ColumnID>& columns, bool unique);
    Result<void> drop_index(IndexID index_id);
    
    Result<IndexMetadata> find_index(IndexID index_id) const;
    Result<std::vector<IndexMetadata>> find_indexes_for_table(TableID table_id) const;

    // Flush current in-memory catalog to disk. 
    Result<void> flush();

private:
    storage::BufferPool* m_buffer_pool;
    PageID m_catalog_root{};

    // ID Allocators
    std::uint32_t m_next_schema_id{1};
    std::uint32_t m_next_table_id{1};
    std::uint32_t m_next_column_id{1};
    std::uint32_t m_next_index_id{1};
    std::uint32_t m_next_constraint_id{1};

    // Synchronization
    mutable std::shared_mutex m_mutex;

    // Caches
    std::unordered_map<std::uint32_t, SchemaMetadata> m_schemas;
    std::unordered_map<std::string, SchemaID> m_schema_names;

    std::unordered_map<std::uint32_t, TableMetadata> m_tables;
    // Map: SchemaID -> (Table Name -> TableID)
    std::unordered_map<std::uint32_t, std::unordered_map<std::string, TableID>> m_table_names;

    std::unordered_map<std::uint32_t, IndexMetadata> m_indexes;
    // Map: TableID -> vector<IndexID>
    std::unordered_map<std::uint32_t, std::vector<IndexID>> m_table_indexes;
    
    Result<void> persist_to_disk();
};

} // namespace nemo::catalog

#endif // NEMO_CATALOG_CATALOG_HPP
