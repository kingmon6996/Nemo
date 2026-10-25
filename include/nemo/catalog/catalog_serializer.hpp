#ifndef NEMO_CATALOG_CATALOG_SERIALIZER_HPP
#define NEMO_CATALOG_CATALOG_SERIALIZER_HPP

#include "nemo/catalog/schema.hpp"
#include "nemo/catalog/table_metadata.hpp"
#include "nemo/catalog/index_metadata.hpp"
#include "nemo/storage/buffer_pool.hpp"
#include "nemo/core/status.hpp"
#include <unordered_map>

namespace nemo::catalog {

class CatalogSerializer {
public:
    static Result<void> save(storage::BufferPool* buffer_pool, PageID root_page,
                             const std::unordered_map<std::uint32_t, SchemaMetadata>& schemas,
                             const std::unordered_map<std::uint32_t, TableMetadata>& tables,
                             const std::unordered_map<std::uint32_t, IndexMetadata>& indexes,
                             std::uint32_t next_schema, std::uint32_t next_table,
                             std::uint32_t next_col, std::uint32_t next_idx, std::uint32_t next_const);

    static Result<void> load(storage::BufferPool* buffer_pool, PageID root_page,
                             std::unordered_map<std::uint32_t, SchemaMetadata>& schemas,
                             std::unordered_map<std::uint32_t, TableMetadata>& tables,
                             std::unordered_map<std::uint32_t, IndexMetadata>& indexes,
                             std::uint32_t& next_schema, std::uint32_t& next_table,
                             std::uint32_t& next_col, std::uint32_t& next_idx, std::uint32_t& next_const);
};

} // namespace nemo::catalog

#endif // NEMO_CATALOG_CATALOG_SERIALIZER_HPP
