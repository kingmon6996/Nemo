#include "nemo/sql/ast/expressions.hpp"
#include "nemo/sql/ast/ast_visitor.hpp"

namespace nemo::sql::ast {

void IdentifierExpression::accept(ASTVisitor& visitor) const { visitor.visit(*this); }
void BinaryExpression::accept(ASTVisitor& visitor) const { visitor.visit(*this); }
void UnaryExpression::accept(ASTVisitor& visitor) const { visitor.visit(*this); }
void FunctionCallExpression::accept(ASTVisitor& visitor) const { visitor.visit(*this); }
void ParameterExpression::accept(ASTVisitor& visitor) const { visitor.visit(*this); }

} // namespace nemo::sql::ast
