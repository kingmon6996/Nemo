#ifndef NEMO_CATALOG_CONSTRAINT_HPP
#define NEMO_CATALOG_CONSTRAINT_HPP

#include "nemo/catalog/catalog_id.hpp"
#include <string>

namespace nemo::catalog {

enum class ConstraintType {
    PrimaryKey,
    Unique,
    ForeignKey,
    Check
};

class Constraint {
public:
    Constraint(ConstraintID id, TableID table_id, std::string name, ConstraintType type);
    virtual ~Constraint() = default;

    ConstraintID id() const { return m_id; }
    TableID table_id() const { return m_table_id; }
    const std::string& name() const { return m_name; }
    ConstraintType type() const { return m_type; }

private:
    ConstraintID m_id;
    TableID m_table_id;
    std::string m_name;
    ConstraintType m_type;
};

} // namespace nemo::catalog

#endif // NEMO_CATALOG_CONSTRAINT_HPP
