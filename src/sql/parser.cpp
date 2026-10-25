#include "nemo/sql/parser.hpp"
#include "nemo/sql/sql_error.hpp"
#include "nemo/sql/ast/expressions.hpp"
#include "nemo/sql/ast/literals.hpp"
#include "nemo/sql/ast/select.hpp"
#include "nemo/core/result.hpp"

namespace nemo::sql {

Parser::Parser(std::string_view sql) {
    Lexer lexer(sql);
    auto tokens_res = lexer.tokenize();
    if (tokens_res) {
        m_tokens = std::move(tokens_res.value());
    }
}

bool Parser::is_at_end() const {
    return peek().type == TokenType::Eof;
}

const Token& Parser::peek() const {
    return m_tokens[m_current];
}

const Token& Parser::previous() const {
    return m_tokens[m_current - 1];
}

bool Parser::check(TokenType type) const {
    if (is_at_end()) return false;
    return peek().type == type;
}

Token Parser::advance() {
    if (!is_at_end()) m_current++;
    return previous();
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

Result<Token> Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    return SQLError::make_error(Status::InvalidArgument, peek().location, message);
}

Precedence Parser::get_precedence(TokenType type) const {
    switch (type) {
        case TokenType::Or: return Precedence::Or;
        case TokenType::And: return Precedence::And;
        case TokenType::Equal:
        case TokenType::NotEqual:
        case TokenType::Is:
        case TokenType::In:
        case TokenType::Like: return Precedence::Equality;
        case TokenType::Less:
        case TokenType::LessEqual:
        case TokenType::Greater:
        case TokenType::GreaterEqual: return Precedence::Comparison;
        case TokenType::Plus:
        case TokenType::Minus:
        case TokenType::Concat: return Precedence::Term;
        case TokenType::Star:
        case TokenType::Slash:
        case TokenType::Percent: return Precedence::Factor;
        default: return Precedence::None;
    }
}

Result<std::unique_ptr<ast::Expression>> Parser::parse_expression(Precedence precedence) {
    if (is_at_end()) return SQLError::make_error(Status::InvalidArgument, peek().location, "Unexpected EOF in expression");
    
    Token current = advance();
    auto prefix_res = parse_prefix(current);
    if (!prefix_res) return prefix_res.error();
    
    std::unique_ptr<ast::Expression> left = std::move(prefix_res.value());
    
    while (precedence < get_precedence(peek().type)) {
        Token infix_token = advance();
        auto infix_res = parse_infix(infix_token, std::move(left));
        if (!infix_res) return infix_res.error();
        left = std::move(infix_res.value());
    }
    
    return left;
}

Result<std::unique_ptr<ast::Expression>> Parser::parse_prefix(const Token& token) {
    switch (token.type) {
        case TokenType::IntegerLiteral: {
            long long val = std::stoll(token.lexeme);
            return std::make_unique<ast::LiteralExpression>(types::Value(static_cast<std::int32_t>(val)));
        }
        case TokenType::StringLiteral:
            return std::make_unique<ast::LiteralExpression>(types::Value(token.lexeme));
        case TokenType::True:
            return std::make_unique<ast::LiteralExpression>(types::Value(true));
        case TokenType::False:
            return std::make_unique<ast::LiteralExpression>(types::Value(false));
        case TokenType::Identifier:
            if (check(TokenType::LeftParen)) {
                // Function call
                advance(); // Consume '('
                std::vector<std::unique_ptr<ast::Expression>> args;
                bool distinct = false;
                if (match(TokenType::Distinct)) distinct = true;
                
                if (!check(TokenType::RightParen)) {
                    do {
                        if (match(TokenType::Star)) {
                            // COUNT(*)
                            break; // special case
                        }
                        auto expr_res = parse_expression(Precedence::Assignment);
                        if (!expr_res) return expr_res.error();
                        args.push_back(std::move(expr_res.value()));
                    } while (match(TokenType::Comma));
                }
                auto res = consume(TokenType::RightParen, "Expected ')' after arguments");
                if (!res) return res.error();
                
                return std::make_unique<ast::FunctionCallExpression>(token.lexeme, std::move(args), distinct);
            }
            return std::make_unique<ast::IdentifierExpression>(token.lexeme);
        
        case TokenType::Parameter:
            return std::make_unique<ast::ParameterExpression>(token.lexeme);
            
        case TokenType::Minus:
        case TokenType::Not: {
            auto right_res = parse_expression(Precedence::Unary);
            if (!right_res) return right_res.error();
            return std::make_unique<ast::UnaryExpression>(token.type, std::move(right_res.value()));
        }
        default:
            return SQLError::make_error(Status::InvalidArgument, token.location, "Unexpected prefix token");
    }
}

Result<std::unique_ptr<ast::Expression>> Parser::parse_infix(const Token& token, std::unique_ptr<ast::Expression> left) {
    Precedence precedence = get_precedence(token.type);
    auto right_res = parse_expression(precedence);
    if (!right_res) return right_res.error();
    
    return std::make_unique<ast::BinaryExpression>(std::move(left), token.type, std::move(right_res.value()));
}

Result<std::unique_ptr<ast::Statement>> Parser::parse_statement() {
    if (m_tokens.empty()) return SQLError::make_error(Status::InvalidArgument, m_tokens[0].location, "No tokens");
    
    if (match(TokenType::Select)) return parse_select();
    
    return SQLError::make_error(Status::InvalidArgument, peek().location, "Unsupported statement type");
}

Result<std::unique_ptr<ast::Statement>> Parser::parse_select() {
    std::vector<ast::ResultColumn> projection;
    
    // Projection
    if (!check(TokenType::From)) {
        do {
            if (match(TokenType::Star)) {
                ast::ResultColumn col;
                col.expr = std::make_unique<ast::IdentifierExpression>("*");
                projection.push_back(std::move(col));
            } else {
                auto expr_res = parse_expression(Precedence::Assignment);
                if (!expr_res) return expr_res.error();
                
                std::string alias = "";
                if (match(TokenType::As)) {
                    auto alias_tok = consume(TokenType::Identifier, "Expected alias after AS");
                    if (!alias_tok) return alias_tok.error();
                    alias = alias_tok.value().lexeme;
                }
                
                ast::ResultColumn col;
                col.expr = std::move(expr_res.value());
                col.alias = alias;
                projection.push_back(std::move(col));
            }
        } while (match(TokenType::Comma));
    }
    
    // FROM
    std::vector<ast::TableReference> from_tables;
    if (match(TokenType::From)) {
        do {
            auto table_tok = consume(TokenType::Identifier, "Expected table name in FROM clause");
            if (!table_tok) return table_tok.error();
            
            ast::TableReference ref;
            ref.table_name = table_tok.value().lexeme;
            
            if (match(TokenType::As)) {
                auto alias_tok = consume(TokenType::Identifier, "Expected alias after AS");
                if (!alias_tok) return alias_tok.error();
                ref.alias = alias_tok.value().lexeme;
            } else if (check(TokenType::Identifier)) {
                ref.alias = advance().lexeme;
            }
            
            from_tables.push_back(std::move(ref));
        } while (match(TokenType::Comma));
    }
    
    // WHERE
    std::unique_ptr<ast::Expression> where_clause = nullptr;
    if (match(TokenType::Where)) {
        auto expr_res = parse_expression(Precedence::Assignment);
        if (!expr_res) return expr_res.error();
        where_clause = std::move(expr_res.value());
    }
    
    return std::make_unique<ast::SelectStatement>(std::move(projection), std::move(from_tables), std::move(where_clause));
}

} // namespace nemo::sql
