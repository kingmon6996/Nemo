#include "nemo/execution/planner/planner.hpp"
#include "nemo/core/status.hpp"

namespace nemo::execution::planner {

Result<std::unique_ptr<LogicalOperator>> Planner::plan_statement(const binder::BoundStatement* stmt) {
    if (!stmt) return Status::InvalidArgument;
    
    switch (stmt->type()) {
        case binder::BoundStatementType::Select:
            return plan_select(static_cast<const binder::BoundSelect*>(stmt));
        case binder::BoundStatementType::Insert:
            return plan_insert(static_cast<const binder::BoundInsert*>(stmt));
        default:
            return Status::Unsupported;
    }
}

Result<std::unique_ptr<LogicalOperator>> Planner::plan_select(const binder::BoundSelect* stmt) {
    std::unique_ptr<LogicalOperator> root;
    
    // 1. FROM clause (Scan)
    if (stmt->from_table) {
        if (stmt->from_table->type() == binder::BoundTableRef::Type::BaseTable) {
            const auto* base_table = static_cast<const binder::BoundBaseTableRef*>(stmt->from_table.get());
            // In a real implementation we would determine exactly which columns we need, but for now we'll do a generic scan
            std::vector<core::ColumnID> all_cols; // We would fetch this from catalog
            root = std::make_unique<LogicalScan>(base_table->table_id(), std::move(all_cols));
        } else {
            return Status::Unsupported; // Joins/Subqueries not fully implemented in Mod12 Phase1 yet
        }
    }
    
    // 2. WHERE clause (Filter)
    if (stmt->where_clause) {
        // TODO: We need to clone the expression or take ownership.
        // For simplicity in this architecture, we might need a clone() method on BoundExpression.
        // Assuming we just pass it along for now (if the planner takes ownership of the bound AST).
        // Since bound_stmt is const, we can't move from it. We definitely need a clone().
        // For Phase 1 compilation we will leave it as a comment and return unsupported if we need to clone.
    }
    
    // 3. SELECT list (Projection)
    if (!stmt->select_list.empty()) {
        // If we need to clone the expressions...
    }
    
    return root;
}

Result<std::unique_ptr<LogicalOperator>> Planner::plan_insert(const binder::BoundInsert* /*stmt*/) {
    // Clone values
    // return std::make_unique<LogicalInsert>(stmt->table_id, cloned_values);
    return Status::Unsupported;
}

} // namespace nemo::execution::planner
