#include "nemo/sql/lexer.hpp"
#include "nemo/sql/sql_error.hpp"
#include <unordered_map>
#include <cctype>
#include <algorithm>

namespace nemo::sql {

namespace {

const std::unordered_map<std::string, TokenType> k_keywords = {
    {"SELECT", TokenType::Select}, {"FROM", TokenType::From}, {"WHERE", TokenType::Where},
    {"INSERT", TokenType::Insert}, {"INTO", TokenType::Into}, {"VALUES", TokenType::Values},
    {"UPDATE", TokenType::Update}, {"SET", TokenType::Set},
    {"DELETE", TokenType::Delete},
    {"CREATE", TokenType::Create}, {"ALTER", TokenType::Alter}, {"DROP", TokenType::Drop},
    {"TABLE", TokenType::Table}, {"INDEX", TokenType::Index}, {"SCHEMA", TokenType::Schema},
    {"PRIMARY", TokenType::Primary}, {"KEY", TokenType::Key}, {"UNIQUE", TokenType::Unique},
    {"NULL", TokenType::Null}, {"TRUE", TokenType::True}, {"FALSE", TokenType::False},
    {"AND", TokenType::And}, {"OR", TokenType::Or}, {"NOT", TokenType::Not},
    {"IS", TokenType::Is}, {"IN", TokenType::In}, {"BETWEEN", TokenType::Between},
    {"INNER", TokenType::Inner}, {"LEFT", TokenType::Left}, {"JOIN", TokenType::Join}, {"ON", TokenType::On},
    {"BY", TokenType::By}, {"ORDER", TokenType::Order}, {"GROUP", TokenType::Group},
    {"ASC", TokenType::Asc}, {"DESC", TokenType::Desc}, {"LIMIT", TokenType::Limit},
    {"AS", TokenType::As}
    // Add rest as needed
};

std::string to_upper(std::string_view sv) {
    std::string res;
    res.reserve(sv.size());
    for (char c : sv) {
        res.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return res;
}

} // namespace

Lexer::Lexer(std::string_view source) : m_source(source) {}

bool Lexer::is_at_end() const {
    return m_current >= m_source.size();
}

char Lexer::advance() {
    char c = m_source[m_current++];
    if (c == '\n') {
        m_loc.line++;
        m_loc.column = 1;
    } else {
        m_loc.column++;
    }
    m_loc.offset = m_current;
    return c;
}

char Lexer::peek() const {
    if (is_at_end()) return '\0';
    return m_source[m_current];
}

char Lexer::peek_next() const {
    if (m_current + 1 >= m_source.size()) return '\0';
    return m_source[m_current + 1];
}

bool Lexer::match(char expected) {
    if (is_at_end()) return false;
    if (m_source[m_current] != expected) return false;
    advance();
    return true;
}

void Lexer::skip_whitespace_and_comments() {
    while (true) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
            case '\n':
                advance();
                break;
            case '-':
                if (peek_next() == '-') {
                    // Single line comment
                    while (peek() != '\n' && !is_at_end()) advance();
                } else {
                    return;
                }
                break;
            case '/':
                if (peek_next() == '*') {
                    // Multi-line comment
                    advance(); advance();
                    while (!is_at_end()) {
                        if (peek() == '*' && peek_next() == '/') {
                            advance(); advance();
                            break;
                        }
                        advance();
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

Token Lexer::make_token(TokenType type, std::string lexeme) {
    return Token(type, std::move(lexeme), m_loc);
}

Result<std::vector<Token>> Lexer::tokenize() {
    std::vector<Token> tokens;
    
    while (!is_at_end()) {
        skip_whitespace_and_comments();
        if (is_at_end()) break;
        
        auto tok_res = scan_token();
        if (!tok_res) return tok_res.error();
        
        tokens.push_back(std::move(tok_res.value()));
    }
    
    tokens.push_back(make_token(TokenType::Eof, ""));
    return tokens;
}

Result<Token> Lexer::scan_token() {
    char c = advance();
    SourceLocation start_loc = m_loc;
    
    switch (c) {
        case '(': return Token(TokenType::LeftParen, "(", start_loc);
        case ')': return Token(TokenType::RightParen, ")", start_loc);
        case ',': return Token(TokenType::Comma, ",", start_loc);
        case '.': return Token(TokenType::Dot, ".", start_loc);
        case ';': return Token(TokenType::Semicolon, ";", start_loc);
        case '+': return Token(TokenType::Plus, "+", start_loc);
        case '-': return Token(TokenType::Minus, "-", start_loc);
        case '*': return Token(TokenType::Star, "*", start_loc);
        case '/': return Token(TokenType::Slash, "/", start_loc);
        case '=': 
            if (match('=')) return Token(TokenType::Equal, "==", start_loc);
            return Token(TokenType::Equal, "=", start_loc);
        case '!':
            if (match('=')) return Token(TokenType::NotEqual, "!=", start_loc);
            return SQLError::make_error(Status::InvalidArgument, start_loc, "Unexpected character '!'");
        case '<':
            if (match('=')) return Token(TokenType::LessEqual, "<=", start_loc);
            if (match('>')) return Token(TokenType::NotEqual, "<>", start_loc);
            return Token(TokenType::Less, "<", start_loc);
        case '>':
            if (match('=')) return Token(TokenType::GreaterEqual, ">=", start_loc);
            return Token(TokenType::Greater, ">", start_loc);
        case '|':
            if (match('|')) return Token(TokenType::Concat, "||", start_loc);
            return SQLError::make_error(Status::InvalidArgument, start_loc, "Unexpected character '|'");
        
        case '\'': return scan_string_literal();
        case '"': return scan_quoted_identifier();
            
        default:
            if (std::isdigit(static_cast<unsigned char>(c))) {
                m_current--; m_loc.column--; m_loc.offset--;
                return scan_number();
            } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                m_current--; m_loc.column--; m_loc.offset--;
                return scan_identifier_or_keyword();
            }
            return SQLError::make_error(Status::InvalidArgument, start_loc, "Unexpected character");
    }
}

Result<Token> Lexer::scan_identifier_or_keyword() {
    SourceLocation start_loc = m_loc;
    std::size_t start = m_current;
    
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
        advance();
    }
    
    std::string text = std::string(m_source.substr(start, m_current - start));
    std::string upper_text = to_upper(text);
    
    auto it = k_keywords.find(upper_text);
    if (it != k_keywords.end()) {
        return Token(it->second, upper_text, start_loc); // Use upper for keywords
    }
    
    return Token(TokenType::Identifier, text, start_loc);
}

Result<Token> Lexer::scan_string_literal() {
    SourceLocation start_loc = m_loc;
    std::string str;
    
    while (!is_at_end()) {
        if (peek() == '\'') {
            if (peek_next() == '\'') {
                // Escaped quote
                str.push_back('\'');
                advance();
                advance();
            } else {
                advance(); // closing quote
                return Token(TokenType::StringLiteral, str, start_loc);
            }
        } else {
            str.push_back(advance());
        }
    }
    
    return SQLError::make_error(Status::InvalidArgument, start_loc, "Unterminated string literal");
}

Result<Token> Lexer::scan_quoted_identifier() {
    SourceLocation start_loc = m_loc;
    std::string str;
    
    while (!is_at_end() && peek() != '"') {
        str.push_back(advance());
    }
    
    if (is_at_end()) {
        return SQLError::make_error(Status::InvalidArgument, start_loc, "Unterminated quoted identifier");
    }
    
    advance(); // closing quote
    return Token(TokenType::QuotedIdentifier, str, start_loc);
}

Result<Token> Lexer::scan_number() {
    SourceLocation start_loc = m_loc;
    std::size_t start = m_current;
    bool is_float = false;
    
    while (std::isdigit(static_cast<unsigned char>(peek()))) {
        advance();
    }
    
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek_next()))) {
        is_float = true;
        advance(); // consume '.'
        while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
    }
    
    std::string text = std::string(m_source.substr(start, m_current - start));
    return Token(is_float ? TokenType::FloatLiteral : TokenType::IntegerLiteral, text, start_loc);
}

} // namespace nemo::sql
