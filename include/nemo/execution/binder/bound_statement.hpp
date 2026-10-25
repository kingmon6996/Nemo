#ifndef NEMO_EXECUTION_BINDER_BOUND_STATEMENT_HPP
#define NEMO_EXECUTION_BINDER_BOUND_STATEMENT_HPP

#include "nemo/execution/binder/bound_expression.hpp"
#include "nemo/core/ids.hpp"
#include "nemo/core/types.hpp"
#include <vector>
#include <memory>
#include <string>

namespace nemo::execution::binder {

enum class BoundStatementType {
    Select,
    Insert,
    Update,
    Delete,
    Create,
    Drop
};

class BoundStatement {
public:
    virtual ~BoundStatement() = default;
    virtual BoundStatementType type() const = 0;
};

// Base class for anything that acts like a table source in a query
class BoundTableRef {
public:
    virtual ~BoundTableRef() = default;
    enum class Type { BaseTable, Subquery, Join };
    virtual Type type() const = 0;
};

class BoundBaseTableRef : public BoundTableRef {
public:
    BoundBaseTableRef(TableID table_id, std::string alias)
        : m_table_id(table_id), m_alias(std::move(alias)) {}
        
    Type type() const override { return Type::BaseTable; }
    TableID table_id() const { return m_table_id; }
    const std::string& alias() const { return m_alias; }
private:
    TableID m_table_id;
    std::string m_alias;
};

class BoundSelect : public BoundStatement {
public:
    BoundStatementType type() const override { return BoundStatementType::Select; }
    
    // SELECT list
    std::vector<std::unique_ptr<BoundExpression>> select_list;
    
    // FROM clause
    std::unique_ptr<BoundTableRef> from_table;
    
    // WHERE clause
    std::unique_ptr<BoundExpression> where_clause;
    
    // GROUP BY clause
    std::vector<std::unique_ptr<BoundExpression>> group_by;
    
    // HAVING clause
    std::unique_ptr<BoundExpression> having_clause;
    
    // We could add ORDER BY, LIMIT, OFFSET here
};

class BoundInsert : public BoundStatement {
public:
    BoundStatementType type() const override { return BoundStatementType::Insert; }
    
    TableID table_id;
    
    // Columns being inserted into
    std::vector<core::ColumnID> target_columns;
    
    // Values being inserted (list of rows, where each row is a list of expressions)
    std::vector<std::vector<std::unique_ptr<BoundExpression>>> values;
    
    // Alternatively, INSERT INTO ... SELECT ...
    std::unique_ptr<BoundSelect> select_statement;
};

class BoundUpdate : public BoundStatement {
public:
    BoundStatementType type() const override { return BoundStatementType::Update; }
    
    TableID table_id;
    
    // ColumnID -> Expression
    std::vector<std::pair<core::ColumnID, std::unique_ptr<BoundExpression>>> assignments;
    
    std::unique_ptr<BoundExpression> where_clause;
};

class BoundDelete : public BoundStatement {
public:
    BoundStatementType type() const override { return BoundStatementType::Delete; }
    
    TableID table_id;
    std::unique_ptr<BoundExpression> where_clause;
};

} // namespace nemo::execution::binder

#endif // NEMO_EXECUTION_BINDER_BOUND_STATEMENT_HPP
