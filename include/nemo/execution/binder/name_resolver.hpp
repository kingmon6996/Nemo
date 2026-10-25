#ifndef NEMO_EXECUTION_BINDER_NAME_RESOLVER_HPP
#define NEMO_EXECUTION_BINDER_NAME_RESOLVER_HPP

#include "nemo/catalog/catalog.hpp"
#include "nemo/execution/binder/bound_expression.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace nemo::execution::binder {

// Maintains scope and aliases for tables and columns during binding
class NameResolver {
public:
    NameResolver(catalog::Catalog* catalog) : m_catalog(catalog) {}

    // Register a table in the current scope
    Status add_table(const std::string& table_name, const std::string& alias = "");

    // Resolve a column name (optionally qualified) to a table and column ID
    Result<BoundColumnRef> resolve_column(const std::string& col_name, const std::string& table_alias = "");

private:
    catalog::Catalog* m_catalog;
    
    struct BoundTableContext {
        TableID table_id;
        std::string table_name;
        std::string alias;
        catalog::TableMetadata metadata;
    };
    
    std::vector<BoundTableContext> m_active_tables;
};

} // namespace nemo::execution::binder

#endif // NEMO_EXECUTION_BINDER_NAME_RESOLVER_HPP
