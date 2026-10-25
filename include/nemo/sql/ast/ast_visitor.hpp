#ifndef NEMO_SQL_AST_AST_VISITOR_HPP
#define NEMO_SQL_AST_AST_VISITOR_HPP

namespace nemo::sql::ast {

class IdentifierExpression;
class BinaryExpression;
class UnaryExpression;
class FunctionCallExpression;
class ParameterExpression;
class LiteralExpression;
class SelectStatement;
class InsertStatement;
class CreateTableStatement;

class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;

    virtual void visit(const IdentifierExpression& node) = 0;
    virtual void visit(const BinaryExpression& node) = 0;
    virtual void visit(const UnaryExpression& node) = 0;
    virtual void visit(const FunctionCallExpression& node) = 0;
    virtual void visit(const ParameterExpression& node) = 0;
    virtual void visit(const LiteralExpression& node) = 0;
    
    virtual void visit(const SelectStatement& node) = 0;
    virtual void visit(const InsertStatement& node) = 0;
    virtual void visit(const CreateTableStatement& node) = 0;
};

} // namespace nemo::sql::ast

#endif // NEMO_SQL_AST_AST_VISITOR_HPP
