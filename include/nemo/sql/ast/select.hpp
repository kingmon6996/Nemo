#ifndef NEMO_SQL_AST_SELECT_HPP
#define NEMO_SQL_AST_SELECT_HPP

#include "nemo/sql/ast/statement.hpp"
#include "nemo/sql/ast/expression.hpp"
#include <vector>
#include <memory>
#include <string>

namespace nemo::sql::ast {

struct ResultColumn {
    std::unique_ptr<Expression> expr;
    std::string alias; // Empty if no alias
};

struct TableReference {
    std::string table_name;
    std::string alias; // Empty if no alias
};

class SelectStatement : public Statement {
public:
    SelectStatement(std::vector<ResultColumn> projection, 
                    std::vector<TableReference> from_tables,
                    std::unique_ptr<Expression> where_clause)
        : m_projection(std::move(projection)), 
          m_from_tables(std::move(from_tables)),
          m_where(std::move(where_clause)) {}

    const std::vector<ResultColumn>& projection() const { return m_projection; }
    const std::vector<TableReference>& from_tables() const { return m_from_tables; }
    const Expression* where_clause() const { return m_where.get(); }
    
    void accept(ASTVisitor& visitor) const override;
private:
    std::vector<ResultColumn> m_projection;
    std::vector<TableReference> m_from_tables;
    std::unique_ptr<Expression> m_where;
    
    // In a complete implementation we would add GROUP BY, HAVING, ORDER BY, LIMIT, JOINs here.
    // Simplifying for Module 8 scope check.
};

} // namespace nemo::sql::ast

#endif // NEMO_SQL_AST_SELECT_HPP
