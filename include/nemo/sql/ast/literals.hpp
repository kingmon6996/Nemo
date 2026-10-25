#ifndef NEMO_SQL_AST_LITERALS_HPP
#define NEMO_SQL_AST_LITERALS_HPP

#include "nemo/sql/ast/expression.hpp"
#include "nemo/types/value.hpp"

namespace nemo::sql::ast {

class LiteralExpression : public Expression {
public:
    explicit LiteralExpression(types::Value val) : m_value(std::move(val)) {}
    const types::Value& value() const { return m_value; }
    
    void accept(ASTVisitor& visitor) const override;
private:
    types::Value m_value;
};

} // namespace nemo::sql::ast

#endif // NEMO_SQL_AST_LITERALS_HPP
