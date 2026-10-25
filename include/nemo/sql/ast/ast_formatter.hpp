#ifndef NEMO_SQL_AST_FORMATTER_HPP
#define NEMO_SQL_AST_FORMATTER_HPP

#include "nemo/sql/ast/ast_visitor.hpp"
#include "nemo/sql/ast/statement.hpp"
#include "nemo/sql/ast/expression.hpp"
#include <string>
#include <sstream>

namespace nemo::sql::ast {

class ASTFormatter : public ASTVisitor {
public:
    std::string format(const Statement& stmt);
    std::string format(const Expression& expr);

    void visit(const IdentifierExpression& node) override;
    void visit(const BinaryExpression& node) override;
    void visit(const UnaryExpression& node) override;
    void visit(const FunctionCallExpression& node) override;
    void visit(const ParameterExpression& node) override;
    void visit(const LiteralExpression& node) override;
    
    void visit(const SelectStatement& node) override;
    void visit(const InsertStatement& node) override;
    void visit(const CreateTableStatement& node) override;

private:
    std::stringstream m_ss;
};

} // namespace nemo::sql::ast

#endif // NEMO_SQL_AST_FORMATTER_HPP
