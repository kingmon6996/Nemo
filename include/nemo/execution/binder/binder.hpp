#ifndef NEMO_EXECUTION_BINDER_BINDER_HPP
#define NEMO_EXECUTION_BINDER_BINDER_HPP

#include "nemo/catalog/catalog.hpp"
#include "nemo/sql/ast/ast_visitor.hpp"
#include "nemo/sql/ast/statement.hpp"
#include "nemo/sql/ast/expression.hpp"
#include "nemo/sql/ast/select.hpp"
#include "nemo/sql/ast/insert.hpp"
#include "nemo/sql/ast/ast_visitor.hpp"
#include "nemo/execution/binder/bound_statement.hpp"
#include "nemo/execution/binder/name_resolver.hpp"

namespace nemo::execution::binder {

// Traverses the raw SQL AST and produces a BoundAST that has types resolved,
// catalog metadata attached, and semantic rules enforced.
class Binder {
public:
    Binder(catalog::Catalog* catalog) : m_resolver(catalog), m_catalog(catalog) {}

    // Main entry point
    Result<std::unique_ptr<BoundStatement>> bind_statement(const sql::ast::Statement* stmt);

private:
    NameResolver m_resolver;
    catalog::Catalog* m_catalog;

    // Statement binding
    Result<std::unique_ptr<BoundStatement>> bind_select(const sql::ast::SelectStatement* stmt);
    Result<std::unique_ptr<BoundStatement>> bind_insert(const sql::ast::InsertStatement* stmt);

    // Clause binding
    Result<std::unique_ptr<BoundTableRef>> bind_table_ref(const sql::ast::ASTNode* table_ref);
    
    // Expression binding
    Result<std::unique_ptr<BoundExpression>> bind_expression(const sql::ast::Expression* expr);
    Result<std::unique_ptr<BoundExpression>> bind_literal(const sql::ast::LiteralExpression* expr);
    Result<std::unique_ptr<BoundExpression>> bind_identifier(const sql::ast::IdentifierExpression* expr);
    Result<std::unique_ptr<BoundExpression>> bind_binary_op(const sql::ast::BinaryExpression* expr);
    
    // Type checking & casting
    Result<std::unique_ptr<BoundExpression>> enforce_type(std::unique_ptr<BoundExpression> expr, types::TypeID target_type);
    types::TypeID resolve_result_type(types::TypeID left, types::TypeID right);
};

} // namespace nemo::execution::binder

#endif // NEMO_EXECUTION_BINDER_BINDER_HPP
