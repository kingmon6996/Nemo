#include "nemo/execution/binder/binder.hpp"
#include "nemo/sql/ast/expressions.hpp"
#include "nemo/sql/ast/statement.hpp"
#include "nemo/sql/ast/select.hpp"
#include "nemo/sql/ast/literals.hpp"
#include "nemo/core/status.hpp"

#include <algorithm>
#include <cctype>

namespace nemo::execution::binder {

Status NameResolver::add_table(const std::string& table_name, const std::string& alias) {
    auto table_res = m_catalog->find_table(catalog::SchemaID(1), table_name);
    if (!table_res) return table_res.error();
    
    BoundTableContext ctx;
    ctx.table_id = table_res.value().table_id;
    ctx.table_name = table_name;
    ctx.alias = alias.empty() ? table_name : alias;
    ctx.metadata = table_res.value();
    
    m_active_tables.push_back(std::move(ctx));
    return Status::OK;
}

Result<BoundColumnRef> NameResolver::resolve_column(const std::string& col_name, const std::string& table_alias) {
    std::vector<const BoundTableContext*> matches;
    
    for (const auto& ctx : m_active_tables) {
        if (!table_alias.empty() && ctx.alias != table_alias) {
            continue;
        }
        
        for (const auto& col : ctx.metadata.schema.columns()) {
            if (col.name() == col_name) {
                matches.push_back(&ctx);
                break;
            }
        }
    }
    
    if (matches.empty()) {
        return Status::NotFound; // Column not found
    }
    if (matches.size() > 1) {
        return Status::InvalidArgument; // Ambiguous column reference
    }
    
    const auto* match_ctx = matches.front();
    const auto& schema = match_ctx->metadata.schema;
    
    for (std::size_t i = 0; i < schema.column_count(); ++i) {
        if (schema.columns()[i].name() == col_name) {
            return BoundColumnRef(
                match_ctx->table_id,
                schema.columns()[i].id(),
                schema.columns()[i].type(),
                static_cast<std::uint32_t>(i)
            );
        }
    }
    
    return Status::InternalError; // Should not happen
}

Result<std::unique_ptr<BoundStatement>> Binder::bind_statement(const sql::ast::Statement* stmt) {
    if (auto select_stmt = dynamic_cast<const sql::ast::SelectStatement*>(stmt)) {
        return bind_select(select_stmt);
    }
    if (auto insert_stmt = dynamic_cast<const sql::ast::InsertStatement*>(stmt)) {
        return bind_insert(insert_stmt);
    }
    return Status::Unsupported;
}

Result<std::unique_ptr<BoundStatement>> Binder::bind_select(const sql::ast::SelectStatement* stmt) {
    auto bound_select = std::make_unique<BoundSelect>();
    
    for (const auto& table_ref : stmt->from_tables()) {
        auto status = m_resolver.add_table(table_ref.table_name, table_ref.alias);
        if (status != Status::OK) return status;
    }
    
    if (stmt->where_clause()) {
        auto where_res = bind_expression(stmt->where_clause());
        if (!where_res) return where_res.error();
        bound_select->where_clause = std::move(where_res.value());
        
        if (bound_select->where_clause->result_type() != types::TypeID::Boolean) {
            return Status::InvalidArgument; // Type mismatch
        }
    }
    
    for (const auto& expr : stmt->projection()) {
        auto bound_expr = bind_expression(expr.expr.get());
        if (!bound_expr) return bound_expr.error();
        bound_select->select_list.push_back(std::move(bound_expr.value()));
    }
    
    return std::unique_ptr<BoundStatement>(std::move(bound_select));
}

Result<std::unique_ptr<BoundStatement>> Binder::bind_insert(const sql::ast::InsertStatement* stmt) {
    auto bound_insert = std::make_unique<BoundInsert>();
    
    auto table_res = m_catalog->find_table(catalog::SchemaID(1), stmt->table_name());
    if (!table_res) return table_res.error();
    
    bound_insert->table_id = table_res.value().table_id;
    
    return std::unique_ptr<BoundStatement>(std::move(bound_insert));
}

Result<std::unique_ptr<BoundExpression>> Binder::bind_expression(const sql::ast::Expression* expr) {
    if (!expr) return Status::InvalidArgument;
    
    if (dynamic_cast<const sql::ast::LiteralExpression*>(expr)) {
        return Status::Unsupported; // Stubbed
    }
    if (dynamic_cast<const sql::ast::IdentifierExpression*>(expr)) {
        return Status::Unsupported; // Stubbed
    }
    if (dynamic_cast<const sql::ast::BinaryExpression*>(expr)) {
        return Status::Unsupported; // Stubbed
    }
    if (auto func_expr = dynamic_cast<const sql::ast::FunctionCallExpression*>(expr)) {
        std::vector<std::unique_ptr<BoundExpression>> bound_args;
        for (const auto& arg : func_expr->args()) {
            auto bound_arg_res = bind_expression(arg.get());
            if (!bound_arg_res) { return bound_arg_res.error(); }
            bound_args.push_back(std::move(bound_arg_res.value()));
        }
        
        // Simple function registry simulation for the required functions
        types::TypeID return_type = types::TypeID::Invalid;
        std::string name = func_expr->name();
        
        // Convert to lowercase for matching
        std::transform(name.begin(), name.end(), name.begin(),
            [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
            
        if (name == "array_length" || name == "dict_size" || name == "json_array_length" || name == "array_position") {
            return_type = types::TypeID::Integer;
        } else if (name == "array_append" || name == "array_prepend" || name == "array_remove" || name == "array_slice" || name == "array_concat") {
            return_type = types::TypeID::Array;
        } else if (name == "array_pop" || name == "dict_get" || name == "json_get") {
            return_type = types::TypeID::JSON; // Represents a variant value conceptually for these
        } else if (name == "dict_set" || name == "dict_remove") {
            return_type = types::TypeID::DICT;
        } else if (name == "json_set" || name == "json_remove") {
            return_type = types::TypeID::JSON;
        } else if (name == "array_contains" || name == "dict_contains" || name == "json_contains") {
            return_type = types::TypeID::Boolean;
        } else if (name == "dict_keys" || name == "dict_values" || name == "dict_items" || name == "json_keys") {
            return_type = types::TypeID::Array;
        } else if (name == "json_type") {
            return_type = types::TypeID::Text;
        } else {
            return Status::NotFound; // Function not found
        }
        
        return std::make_unique<BoundFunction>(func_expr->name(), std::move(bound_args), return_type);
    }
    
    return Status::Unsupported;
}

} // namespace nemo::execution::binder
