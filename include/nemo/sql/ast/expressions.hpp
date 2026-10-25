#ifndef NEMO_SQL_AST_EXPRESSIONS_HPP
#define NEMO_SQL_AST_EXPRESSIONS_HPP

#include "nemo/sql/ast/expression.hpp"
#include "nemo/sql/token_type.hpp"
#include <memory>
#include <vector>

namespace nemo::sql::ast {

class IdentifierExpression : public Expression {
public:
    explicit IdentifierExpression(std::string name) : m_name(std::move(name)) {}
    const std::string& name() const { return m_name; }
    void accept(ASTVisitor& visitor) const override;
private:
    std::string m_name;
};

class BinaryExpression : public Expression {
public:
    BinaryExpression(std::unique_ptr<Expression> left, TokenType op, std::unique_ptr<Expression> right)
        : m_left(std::move(left)), m_op(op), m_right(std::move(right)) {}
        
    const Expression* left() const { return m_left.get(); }
    TokenType op() const { return m_op; }
    const Expression* right() const { return m_right.get(); }
    
    void accept(ASTVisitor& visitor) const override;
private:
    std::unique_ptr<Expression> m_left;
    TokenType m_op;
    std::unique_ptr<Expression> m_right;
};

class UnaryExpression : public Expression {
public:
    UnaryExpression(TokenType op, std::unique_ptr<Expression> operand)
        : m_op(op), m_operand(std::move(operand)) {}
        
    TokenType op() const { return m_op; }
    const Expression* operand() const { return m_operand.get(); }
    
    void accept(ASTVisitor& visitor) const override;
private:
    TokenType m_op;
    std::unique_ptr<Expression> m_operand;
};

class FunctionCallExpression : public Expression {
public:
    FunctionCallExpression(std::string name, std::vector<std::unique_ptr<Expression>> args, bool is_distinct)
        : m_name(std::move(name)), m_args(std::move(args)), m_is_distinct(is_distinct) {}
        
    const std::string& name() const { return m_name; }
    const std::vector<std::unique_ptr<Expression>>& args() const { return m_args; }
    bool is_distinct() const { return m_is_distinct; }
    
    void accept(ASTVisitor& visitor) const override;
private:
    std::string m_name;
    std::vector<std::unique_ptr<Expression>> m_args;
    bool m_is_distinct;
};

// Simplified Parameter
class ParameterExpression : public Expression {
public:
    explicit ParameterExpression(std::string name) : m_name(std::move(name)) {}
    const std::string& name() const { return m_name; }
    void accept(ASTVisitor& visitor) const override;
private:
    std::string m_name;
};

} // namespace nemo::sql::ast

#endif // NEMO_SQL_AST_EXPRESSIONS_HPP
