#ifndef NEMO_SQL_AST_STATEMENT_HPP
#define NEMO_SQL_AST_STATEMENT_HPP

#include "nemo/sql/ast/ast_node.hpp"

namespace nemo::sql::ast {

class ASTVisitor;

class Statement : public ASTNode {
public:
    virtual ~Statement() = default;
    
    // Visitor pattern
    virtual void accept(ASTVisitor& visitor) const = 0;
};

} // namespace nemo::sql::ast

#endif // NEMO_SQL_AST_STATEMENT_HPP
