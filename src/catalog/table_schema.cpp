#include "nemo/catalog/table_schema.hpp"

namespace nemo::catalog {

TableSchema::TableSchema(std::vector<Column> columns) : m_columns(std::move(columns)) {
    rebuild_indexes();
}

void TableSchema::rebuild_indexes() {
    m_id_to_index.clear();
    m_name_to_index.clear();
    
    for (std::size_t i = 0; i < m_columns.size(); ++i) {
        m_id_to_index[m_columns[i].id()] = static_cast<std::uint32_t>(i);
        m_name_to_index[m_columns[i].name()] = static_cast<std::uint32_t>(i);
    }
}

void TableSchema::add_column(Column col) {
    if (m_name_to_index.find(col.name()) != m_name_to_index.end()) {
        throw std::runtime_error("Duplicate column name: " + col.name());
    }
    if (m_id_to_index.find(col.id()) != m_id_to_index.end()) {
        throw std::runtime_error("Duplicate column ID");
    }
    
    std::uint32_t idx = static_cast<std::uint32_t>(m_columns.size());
    m_id_to_index[col.id()] = idx;
    m_name_to_index[col.name()] = idx;
    m_columns.push_back(std::move(col));
}

const Column& TableSchema::column_by_ordinal(std::uint32_t ordinal) const {
    if (ordinal >= m_columns.size()) {
        throw std::out_of_range("Column ordinal out of range");
    }
    return m_columns[ordinal];
}

const Column& TableSchema::column_by_id(ColumnID id) const {
    auto it = m_id_to_index.find(id);
    if (it == m_id_to_index.end()) {
        throw std::out_of_range("Column ID not found");
    }
    return m_columns[it->second];
}

const Column& TableSchema::column_by_name(const std::string& name) const {
    auto it = m_name_to_index.find(name);
    if (it == m_name_to_index.end()) {
        throw std::out_of_range("Column name not found: " + name);
    }
    return m_columns[it->second];
}

ColumnID TableSchema::get_column_id(const std::string& name) const {
    auto it = m_name_to_index.find(name);
    if (it == m_name_to_index.end()) {
        return ColumnID(0); // Assuming 0 is invalid contextually or check elsewhere
    }
    return m_columns[it->second].id();
}

} // namespace nemo::catalog
