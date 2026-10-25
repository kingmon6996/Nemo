#ifndef NEMO_EXECUTION_PLANNER_PLANNER_HPP
#define NEMO_EXECUTION_PLANNER_PLANNER_HPP

#include "nemo/execution/binder/bound_statement.hpp"
#include "nemo/execution/planner/logical_operator.hpp"
#include "nemo/core/result.hpp"

namespace nemo::execution::planner {

// Translates a BoundStatement into a LogicalOperator tree
class Planner {
public:
    Planner() = default;

    Result<std::unique_ptr<LogicalOperator>> plan_statement(const binder::BoundStatement* stmt);

private:
    Result<std::unique_ptr<LogicalOperator>> plan_select(const binder::BoundSelect* stmt);
    Result<std::unique_ptr<LogicalOperator>> plan_insert(const binder::BoundInsert* stmt);
};

} // namespace nemo::execution::planner

#endif // NEMO_EXECUTION_PLANNER_PLANNER_HPP
