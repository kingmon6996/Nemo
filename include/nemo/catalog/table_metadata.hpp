#ifndef NEMO_CATALOG_TABLE_METADATA_HPP
#define NEMO_CATALOG_TABLE_METADATA_HPP

#include "nemo/catalog/catalog_id.hpp"
#include "nemo/catalog/table_schema.hpp"
#include "nemo/core/ids.hpp"
#include <string>

namespace nemo::catalog {

struct TableMetadata {
    TableID table_id;
    SchemaID schema_id;
    std::string name;
    
    TableSchema schema;
    std::uint32_t version{1};
    
    // The starting page of the TableHeap that actually contains the records
    PageID first_page_id{};
};

} // namespace nemo::catalog

#endif // NEMO_CATALOG_TABLE_METADATA_HPP
