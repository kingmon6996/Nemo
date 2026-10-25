#ifndef NEMO_SQL_AST_NODE_HPP
#define NEMO_SQL_AST_NODE_HPP

#include "nemo/sql/source_location.hpp"

namespace nemo::sql::ast {

class ASTNode {
public:
    virtual ~ASTNode() = default;

    SourceLocation location() const noexcept { return m_location; }
    void set_location(SourceLocation loc) { m_location = loc; }

private:
    SourceLocation m_location;
};

} // namespace nemo::sql::ast

#endif // NEMO_SQL_AST_NODE_HPP
