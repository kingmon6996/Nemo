#ifndef NEMO_CATALOG_FOREIGN_KEY_HPP
#define NEMO_CATALOG_FOREIGN_KEY_HPP

#include "nemo/catalog/constraint.hpp"
#include <vector>

namespace nemo::catalog {

enum class ForeignKeyAction {
    NoAction,
    Restrict,
    Cascade,
    SetNull,
    SetDefault
};

class ForeignKeyConstraint : public Constraint {
public:
    ForeignKeyConstraint(ConstraintID id, TableID table_id, std::string name, 
                         std::vector<ColumnID> source_cols,
                         TableID ref_table_id, std::vector<ColumnID> ref_cols,
                         ForeignKeyAction on_delete, ForeignKeyAction on_update)
        : Constraint(id, table_id, std::move(name), ConstraintType::ForeignKey),
          m_source_columns(std::move(source_cols)),
          m_referenced_table(ref_table_id),
          m_referenced_columns(std::move(ref_cols)),
          m_on_delete(on_delete), m_on_update(on_update) {}

    const std::vector<ColumnID>& source_columns() const { return m_source_columns; }
    TableID referenced_table() const { return m_referenced_table; }
    const std::vector<ColumnID>& referenced_columns() const { return m_referenced_columns; }
    
    ForeignKeyAction on_delete() const { return m_on_delete; }
    ForeignKeyAction on_update() const { return m_on_update; }

private:
    std::vector<ColumnID> m_source_columns;
    TableID m_referenced_table;
    std::vector<ColumnID> m_referenced_columns;
    
    ForeignKeyAction m_on_delete;
    ForeignKeyAction m_on_update;
};

} // namespace nemo::catalog

#endif // NEMO_CATALOG_FOREIGN_KEY_HPP
