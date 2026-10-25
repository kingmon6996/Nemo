#ifndef NEMO_CATALOG_INDEX_METADATA_HPP
#define NEMO_CATALOG_INDEX_METADATA_HPP

#include "nemo/catalog/catalog_id.hpp"
#include "nemo/core/ids.hpp"
#include <string>
#include <vector>

namespace nemo::catalog {

enum class IndexType {
    BPlusTree
};

struct IndexMetadata {
    IndexID index_id;
    SchemaID schema_id;
    TableID table_id;
    
    std::string name;
    std::vector<ColumnID> indexed_columns;
    
    // Root page of the B+ Tree
    PageID root_page_id{};
    std::uint32_t tree_height{0};
    
    bool is_unique{false};
    bool is_primary_key{false};
    IndexType type{IndexType::BPlusTree};
};

} // namespace nemo::catalog

#endif // NEMO_CATALOG_INDEX_METADATA_HPP
