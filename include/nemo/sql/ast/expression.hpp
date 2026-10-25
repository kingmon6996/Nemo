#ifndef NEMO_SQL_AST_EXPRESSION_HPP
#define NEMO_SQL_AST_EXPRESSION_HPP

#include "nemo/sql/ast/ast_node.hpp"
#include <string>

namespace nemo::sql::ast {

class ASTVisitor;

class Expression : public ASTNode {
public:
    virtual ~Expression() = default;
    
    // Accept method for Visitor pattern
    virtual void accept(ASTVisitor& visitor) const = 0;
};

} // namespace nemo::sql::ast

#endif // NEMO_SQL_AST_EXPRESSION_HPP
