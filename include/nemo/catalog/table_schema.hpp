#ifndef NEMO_CATALOG_TABLE_SCHEMA_HPP
#define NEMO_CATALOG_TABLE_SCHEMA_HPP

#include "nemo/catalog/column.hpp"
#include <vector>
#include <unordered_map>
#include <string>
#include <stdexcept>

namespace nemo::catalog {

class TableSchema {
public:
    TableSchema() = default;
    explicit TableSchema(std::vector<Column> columns);

    const std::vector<Column>& columns() const { return m_columns; }
    std::size_t column_count() const { return m_columns.size(); }

    const Column& column_by_ordinal(std::uint32_t ordinal) const;
    const Column& column_by_id(ColumnID id) const;
    const Column& column_by_name(const std::string& name) const;
    
    // Returns invalid ID if not found
    ColumnID get_column_id(const std::string& name) const;
    
    void add_column(Column col);

private:
    std::vector<Column> m_columns;
    
    // O(1) Lookups
    std::unordered_map<std::uint32_t, std::uint32_t> m_id_to_index;
    std::unordered_map<std::string, std::uint32_t> m_name_to_index;
    
    void rebuild_indexes();
};

} // namespace nemo::catalog

#endif // NEMO_CATALOG_TABLE_SCHEMA_HPP
