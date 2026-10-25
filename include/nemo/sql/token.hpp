#ifndef NEMO_SQL_TOKEN_HPP
#define NEMO_SQL_TOKEN_HPP

#include "nemo/sql/token_type.hpp"
#include "nemo/sql/source_location.hpp"
#include <string>

namespace nemo::sql {

struct Token {
    TokenType type{TokenType::Eof};
    std::string lexeme;
    SourceLocation location;

    Token() = default;
    Token(TokenType t, std::string l, SourceLocation loc)
        : type(t), lexeme(std::move(l)), location(loc) {}
};

} // namespace nemo::sql

#endif // NEMO_SQL_TOKEN_HPP
