#ifndef NEMO_SQL_LEXER_HPP
#define NEMO_SQL_LEXER_HPP

#include "nemo/sql/token.hpp"
#include "nemo/core/status.hpp"
#include "nemo/core/result.hpp"
#include <string_view>
#include <vector>

namespace nemo::sql {

class Lexer {
public:
    explicit Lexer(std::string_view source);

    Result<std::vector<Token>> tokenize();

private:
    std::string_view m_source;
    std::size_t m_current{0};
    SourceLocation m_loc;

    bool is_at_end() const;
    char advance();
    char peek() const;
    char peek_next() const;
    bool match(char expected);

    void skip_whitespace_and_comments();

    Result<Token> scan_token();
    Result<Token> scan_identifier_or_keyword();
    Result<Token> scan_quoted_identifier();
    Result<Token> scan_string_literal();
    Result<Token> scan_number();

    Token make_token(TokenType type, std::string lexeme);
};

} // namespace nemo::sql

#endif // NEMO_SQL_LEXER_HPP
