#ifndef NEMO_EXECUTION_PLANNER_LOGICAL_OPERATOR_HPP
#define NEMO_EXECUTION_PLANNER_LOGICAL_OPERATOR_HPP

#include "nemo/execution/binder/bound_expression.hpp"
#include <memory>
#include "nemo/core/ids.hpp"
#include "nemo/core/types.hpp"
#include <vector>
#include <string>

namespace nemo::execution::planner {

enum class LogicalOperatorType {
    Scan,
    Filter,
    Projection,
    Join,
    Aggregate,
    Insert,
    Update,
    Delete
};

class LogicalOperator {
public:
    virtual ~LogicalOperator() = default;
    virtual LogicalOperatorType type() const = 0;
    
    // Add child operator
    void add_child(std::unique_ptr<LogicalOperator> child) {
        m_children.push_back(std::move(child));
    }
    
    // Get children
    const std::vector<std::unique_ptr<LogicalOperator>>& children() const {
        return m_children;
    }

protected:
    std::vector<std::unique_ptr<LogicalOperator>> m_children;
};

class LogicalScan : public LogicalOperator {
public:
    LogicalScan(TableID table_id, std::vector<core::ColumnID> columns)
        : m_table_id(table_id), m_columns(std::move(columns)) {}
        
    LogicalOperatorType type() const override { return LogicalOperatorType::Scan; }
    
    TableID table_id() const { return m_table_id; }
    const std::vector<core::ColumnID>& columns() const { return m_columns; }

private:
    TableID m_table_id;
    std::vector<core::ColumnID> m_columns;
};

class LogicalFilter : public LogicalOperator {
public:
    LogicalFilter(std::unique_ptr<binder::BoundExpression> predicate)
        : m_predicate(std::move(predicate)) {}
        
    LogicalOperatorType type() const override { return LogicalOperatorType::Filter; }
    
    const binder::BoundExpression* predicate() const { return m_predicate.get(); }
    std::unique_ptr<binder::BoundExpression>& mutable_predicate() { return m_predicate; }

private:
    std::unique_ptr<binder::BoundExpression> m_predicate;
};

class LogicalProjection : public LogicalOperator {
public:
    LogicalProjection(std::vector<std::unique_ptr<binder::BoundExpression>> expressions)
        : m_expressions(std::move(expressions)) {}
        
    LogicalOperatorType type() const override { return LogicalOperatorType::Projection; }
    
    const std::vector<std::unique_ptr<binder::BoundExpression>>& expressions() const { return m_expressions; }

private:
    std::vector<std::unique_ptr<binder::BoundExpression>> m_expressions;
};

class LogicalInsert : public LogicalOperator {
public:
    LogicalInsert(TableID table_id, std::vector<std::vector<std::unique_ptr<binder::BoundExpression>>> values)
        : m_table_id(table_id), m_values(std::move(values)) {}
        
    LogicalOperatorType type() const override { return LogicalOperatorType::Insert; }
    
    TableID table_id() const { return m_table_id; }
    const std::vector<std::vector<std::unique_ptr<binder::BoundExpression>>>& values() const { return m_values; }

private:
    TableID m_table_id;
    std::vector<std::vector<std::unique_ptr<binder::BoundExpression>>> m_values;
};

} // namespace nemo::execution::planner

#endif // NEMO_EXECUTION_PLANNER_LOGICAL_OPERATOR_HPP
