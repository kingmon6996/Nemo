#include "nemo/catalog/catalog.hpp"
#include "nemo/catalog/catalog_serializer.hpp"
#include "nemo/storage/table_heap.hpp"
#include "nemo/index/bplus_tree.hpp"

namespace nemo::catalog {

Catalog::Catalog(storage::BufferPool* buffer_pool) 
    : m_buffer_pool(buffer_pool) {}

Result<void> Catalog::init_new(PageID* out_catalog_root) {
    std::unique_lock lock(m_mutex);
    
    // Allocate catalog root
    auto root_res = m_buffer_pool->allocate_page();
    if (!root_res) return root_res.error();
    
    m_catalog_root = root_res.value().get().id();
    *out_catalog_root = m_catalog_root;
    
    // Create default 'main' schema
    SchemaMetadata main_schema;
    main_schema.schema_id = SchemaID(m_next_schema_id++);
    main_schema.name = "main";
    
    m_schemas[main_schema.schema_id.value()] = main_schema;
    m_schema_names[main_schema.name] = main_schema.schema_id;
    
    return persist_to_disk();
}

Result<void> Catalog::load(PageID catalog_root) {
    std::unique_lock lock(m_mutex);
    m_catalog_root = catalog_root;
    
    // In a real system, we would read the catalog_root page, decode via CatalogSerializer.
    // For V1, assume CatalogSerializer::load does this.
    auto result = CatalogSerializer::load(m_buffer_pool, m_catalog_root, 
                                          m_schemas, m_tables, m_indexes,
                                          m_next_schema_id, m_next_table_id, 
                                          m_next_column_id, m_next_index_id, m_next_constraint_id);
    
    if (!result) return result.error();
    
    // Rebuild lookup caches
    m_schema_names.clear();
    m_table_names.clear();
    m_table_indexes.clear();
    
    for (const auto& [id, schema] : m_schemas) {
        m_schema_names[schema.name] = schema.schema_id;
    }
    
    for (const auto& [id, table] : m_tables) {
        m_table_names[table.schema_id.value()][table.name] = table.table_id;
    }
    
    for (const auto& [id, index] : m_indexes) {
        m_table_indexes[static_cast<std::uint32_t>(index.table_id.value())].push_back(index.index_id);
    }
    
    return {};
}

Result<void> Catalog::flush() {
    std::unique_lock lock(m_mutex);
    return persist_to_disk();
}

Result<void> Catalog::persist_to_disk() {
    return CatalogSerializer::save(m_buffer_pool, m_catalog_root, 
                                   m_schemas, m_tables, m_indexes,
                                   m_next_schema_id, m_next_table_id, 
                                   m_next_column_id, m_next_index_id, m_next_constraint_id);
}

Result<SchemaID> Catalog::create_schema(const std::string& name) {
    std::unique_lock lock(m_mutex);
    
    if (m_schema_names.find(name) != m_schema_names.end()) {
        return nemo::Status::AlreadyExists;
    }
    
    SchemaMetadata sm;
    sm.schema_id = SchemaID(m_next_schema_id++);
    sm.name = name;
    
    m_schemas[sm.schema_id.value()] = sm;
    m_schema_names[sm.name] = sm.schema_id;
    
    auto res = persist_to_disk();
    if (!res) return res.error();
    
    return sm.schema_id;
}

Result<SchemaMetadata> Catalog::find_schema(const std::string& name) const {
    std::shared_lock lock(m_mutex);
    
    auto it = m_schema_names.find(name);
    if (it == m_schema_names.end()) {
        return nemo::Status::NotFound;
    }
    
    return m_schemas.at(static_cast<std::uint32_t>(it->second.value()));
}

Result<TableID> Catalog::create_table(SchemaID schema_id, const std::string& table_name, const TableSchema& schema) {
    std::unique_lock lock(m_mutex);
    
    if (m_schemas.find(static_cast<std::uint32_t>(schema_id.value())) == m_schemas.end()) {
        return nemo::Status::NotFound;
    }
    
    auto& schema_tables = m_table_names[static_cast<std::uint32_t>(schema_id.value())];
    if (schema_tables.find(table_name) != schema_tables.end()) {
        return nemo::Status::AlreadyExists;
    }
    
    // Assign column IDs
    TableSchema final_schema;
    for (std::size_t i = 0; i < schema.column_count(); ++i) {
        Column col = schema.column_by_ordinal(static_cast<std::uint32_t>(i));
        Column new_col(ColumnID(m_next_column_id++), col.name(), col.type(), col.ordinal(), col.is_nullable());
        new_col.set_parameters(col.parameters());
        if (col.default_value().has_value()) {
            new_col.set_default_value(col.default_value());
        }
        final_schema.add_column(new_col);
    }
    
    TableMetadata tm;
    tm.table_id = TableID(m_next_table_id++);
    tm.schema_id = schema_id;
    tm.name = table_name;
    tm.schema = final_schema;
    
    // Create physical TableHeap
    auto heap_res = storage::TableHeap::create(m_buffer_pool);
    if (!heap_res) return heap_res.error();
    
    tm.first_page_id = heap_res.value().first_page_id();
    
    m_tables[static_cast<std::uint32_t>(tm.table_id.value())] = tm;
    schema_tables[table_name] = tm.table_id;
    
    auto res = persist_to_disk();
    if (!res) return res.error();
    
    return tm.table_id;
}

Result<void> Catalog::drop_table(TableID table_id) {
    std::unique_lock lock(m_mutex);
    
    auto it = m_tables.find(static_cast<std::uint32_t>(table_id.value()));
    if (it == m_tables.end()) {
        return nemo::Status::NotFound;
    }
    
    // Find indexes
    auto idx_it = m_table_indexes.find(static_cast<std::uint32_t>(table_id.value()));
    if (idx_it != m_table_indexes.end()) {
        for (IndexID i_id : idx_it->second) {
            m_indexes.erase(static_cast<std::uint32_t>(i_id.value()));
        }
        m_table_indexes.erase(idx_it);
    }
    
    // Cleanup TableHeap physical pages goes here (TODO: Module 5/8 GC)
    
    // Remove from caches
    m_table_names[static_cast<std::uint32_t>(it->second.schema_id.value())].erase(it->second.name);
    m_tables.erase(it);
    
    return persist_to_disk();
}

Result<TableMetadata> Catalog::find_table(TableID table_id) const {
    std::shared_lock lock(m_mutex);
    
    auto it = m_tables.find(static_cast<std::uint32_t>(table_id.value()));
    if (it == m_tables.end()) {
        return nemo::Status::NotFound;
    }
    
    return it->second;
}

Result<TableMetadata> Catalog::find_table(SchemaID schema_id, const std::string& table_name) const {
    std::shared_lock lock(m_mutex);
    
    auto s_it = m_table_names.find(static_cast<std::uint32_t>(schema_id.value()));
    if (s_it == m_table_names.end()) {
        return nemo::Status::NotFound;
    }
    
    auto t_it = s_it->second.find(table_name);
    if (t_it == s_it->second.end()) {
        return nemo::Status::NotFound;
    }
    
    return m_tables.at(static_cast<std::uint32_t>(t_it->second.value()));
}

Result<IndexID> Catalog::create_index(TableID table_id, const std::string& index_name, 
                                      const std::vector<ColumnID>& columns, bool unique) {
    std::unique_lock lock(m_mutex);
    
    auto t_it = m_tables.find(static_cast<std::uint32_t>(table_id.value()));
    if (t_it == m_tables.end()) {
        return nemo::Status::NotFound;
    }
    
    // Name conflict check (within schema)
    for (const auto& [id, idx] : m_indexes) {
        if (idx.schema_id == t_it->second.schema_id && idx.name == index_name) {
            return nemo::Status::AlreadyExists;
        }
    }
    
    IndexMetadata im;
    im.index_id = IndexID(m_next_index_id++);
    im.schema_id = t_it->second.schema_id;
    im.table_id = table_id;
    im.name = index_name;
    im.indexed_columns = columns;
    im.is_unique = unique;
    
    // Create physical B+ Tree
    index::IndexMetadata physical_im;
    physical_im.index_id = static_cast<std::uint32_t>(im.index_id.value());
    physical_im.name = im.name;
    physical_im.root_page_id = im.root_page_id;
    physical_im.tree_height = im.tree_height;
    physical_im.is_unique = im.is_unique;
    
    // Determine types for index
    for (auto cid : columns) {
        physical_im.key_types.push_back(t_it->second.schema.column_by_id(cid).type());
    }
    
    auto tree_res = index::BPlusTree::create(m_buffer_pool, physical_im);
    if (!tree_res) return tree_res.error();
    
    // Tree might have invalid root page until first insert, that's fine.
    // If it allocated one, we save it.
    im.root_page_id = tree_res.value()->metadata().root_page_id;
    im.tree_height = tree_res.value()->metadata().tree_height;
    
    m_indexes[static_cast<std::uint32_t>(im.index_id.value())] = im;
    m_table_indexes[static_cast<std::uint32_t>(table_id.value())].push_back(im.index_id);
    
    auto res = persist_to_disk();
    if (!res) return res.error();
    
    return im.index_id;
}

Result<void> Catalog::validate() const {
    std::shared_lock lock(m_mutex);
    
    // Verify foreign keys, index references, etc.
    for (const auto& [id, idx] : m_indexes) {
        if (m_tables.find(static_cast<std::uint32_t>(idx.table_id.value())) == m_tables.end()) {
            return nemo::Status::Corruption; // Dangling index
        }
        for (auto cid : idx.indexed_columns) {
            try {
                m_tables.at(static_cast<std::uint32_t>(idx.table_id.value())).schema.column_by_id(cid);
            } catch (...) {
                return nemo::Status::Corruption;
            }
        }
    }
    
    return {};
}

} // namespace nemo::catalog

