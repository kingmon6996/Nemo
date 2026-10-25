#ifndef NEMO_SQL_AST_DATA_DEFINITION_HPP
#define NEMO_SQL_AST_DATA_DEFINITION_HPP

#include "nemo/sql/ast/statement.hpp"
#include "nemo/catalog/column.hpp"
#include <vector>
#include <string>

namespace nemo::sql::ast {

class CreateTableStatement : public Statement {
public:
    CreateTableStatement(std::string table_name, std::vector<catalog::Column> columns)
        : m_table_name(std::move(table_name)), m_columns(std::move(columns)) {}

    const std::string& table_name() const { return m_table_name; }
    const std::vector<catalog::Column>& columns() const { return m_columns; }
    
    void accept(ASTVisitor& visitor) const override;
private:
    std::string m_table_name;
    std::vector<catalog::Column> m_columns;
};

// Similar ASTs for CreateIndex, DropTable, etc would go here...

} // namespace nemo::sql::ast

#endif // NEMO_SQL_AST_DATA_DEFINITION_HPP
