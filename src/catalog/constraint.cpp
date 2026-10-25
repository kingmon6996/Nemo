#include "nemo/catalog/constraint.hpp"

namespace nemo::catalog {

Constraint::Constraint(ConstraintID id, TableID table_id, std::string name, ConstraintType type)
    : m_id(id), m_table_id(table_id), m_name(std::move(name)), m_type(type) {}

} // namespace nemo::catalog
