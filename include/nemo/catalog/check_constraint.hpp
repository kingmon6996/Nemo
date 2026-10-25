#ifndef NEMO_CATALOG_CHECK_CONSTRAINT_HPP
#define NEMO_CATALOG_CHECK_CONSTRAINT_HPP

#include "nemo/catalog/constraint.hpp"

namespace nemo::catalog {

class CheckConstraint : public Constraint {
public:
    CheckConstraint(ConstraintID id, TableID table_id, std::string name, std::string expression)
        : Constraint(id, table_id, std::move(name), ConstraintType::Check),
          m_expression(std::move(expression)) {}

    const std::string& expression() const { return m_expression; }

private:
    std::string m_expression; // Serialized AST or expression string placeholder
};

} // namespace nemo::catalog

#endif // NEMO_CATALOG_CHECK_CONSTRAINT_HPP
