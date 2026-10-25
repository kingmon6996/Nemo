#include "nemo/sql/ast/ast_formatter.hpp"
#include "nemo/sql/ast/expressions.hpp"
#include "nemo/sql/ast/literals.hpp"
#include "nemo/sql/ast/select.hpp"

namespace nemo::sql::ast {

std::string ASTFormatter::format(const Statement& stmt) {
    m_ss.str("");
    m_ss.clear();
    stmt.accept(*this);
    return m_ss.str();
}

std::string ASTFormatter::format(const Expression& expr) {
    m_ss.str("");
    m_ss.clear();
    expr.accept(*this);
    return m_ss.str();
}

void ASTFormatter::visit(const IdentifierExpression& node) {
    m_ss << node.name();
}

void ASTFormatter::visit(const BinaryExpression& node) {
    node.left()->accept(*this);
    m_ss << " " << to_string(node.op()) << " ";
    node.right()->accept(*this);
}

void ASTFormatter::visit(const UnaryExpression& node) {
    m_ss << to_string(node.op()) << " ";
    node.operand()->accept(*this);
}

void ASTFormatter::visit(const FunctionCallExpression& node) {
    m_ss << node.name() << "(";
    if (node.is_distinct()) m_ss << "DISTINCT ";
    for (std::size_t i = 0; i < node.args().size(); ++i) {
        node.args()[i]->accept(*this);
        if (i < node.args().size() - 1) m_ss << ", ";
    }
    m_ss << ")";
}

void ASTFormatter::visit(const ParameterExpression& node) {
    m_ss << node.name();
}

void ASTFormatter::visit(const LiteralExpression& /*node*/) {
    // Basic value formatting for AST
    // Assuming to_string or similar exists for Value
    // We will just write a placeholder for module 8 implementation bounds
    m_ss << "?LITERAL?"; 
}

void ASTFormatter::visit(const SelectStatement& node) {
    m_ss << "SELECT ";
    for (std::size_t i = 0; i < node.projection().size(); ++i) {
        node.projection()[i].expr->accept(*this);
        if (!node.projection()[i].alias.empty()) {
            m_ss << " AS " << node.projection()[i].alias;
        }
        if (i < node.projection().size() - 1) m_ss << ", ";
    }
    
    if (!node.from_tables().empty()) {
        m_ss << " FROM ";
        for (std::size_t i = 0; i < node.from_tables().size(); ++i) {
            m_ss << node.from_tables()[i].table_name;
            if (!node.from_tables()[i].alias.empty()) {
                m_ss << " " << node.from_tables()[i].alias;
            }
            if (i < node.from_tables().size() - 1) m_ss << ", ";
        }
    }
    
    if (node.where_clause()) {
        m_ss << " WHERE ";
        node.where_clause()->accept(*this);
    }
}

void ASTFormatter::visit(const InsertStatement& /*node*/) {}
void ASTFormatter::visit(const CreateTableStatement& /*node*/) {}

} // namespace nemo::sql::ast
