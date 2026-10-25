#include "nemo/sql/ast/literals.hpp"
#include "nemo/sql/ast/ast_visitor.hpp"

namespace nemo::sql::ast {

void LiteralExpression::accept(ASTVisitor& visitor) const {
    visitor.visit(*this);
}

} // namespace nemo::sql::ast
