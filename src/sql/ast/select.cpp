#include "nemo/sql/ast/select.hpp"
#include "nemo/sql/ast/ast_visitor.hpp"

namespace nemo::sql::ast {

void SelectStatement::accept(ASTVisitor& visitor) const {
    visitor.visit(*this);
}

} // namespace nemo::sql::ast
