#ifndef NEMO_SQL_TOKEN_TYPE_HPP
#define NEMO_SQL_TOKEN_TYPE_HPP

#include <string_view>

namespace nemo::sql {

enum class TokenType {
    // Special
    Eof,
    Invalid,

    // Identifiers and Literals
    Identifier,
    QuotedIdentifier,
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,
    BlobLiteral,
    Parameter, // e.g. '?' or ':name'

    // Keywords
    Null, True, False,
    And, Or, Not, Is, In, Between, Like, Glob, Regexp,
    As, Distinct, All,
    Select, From, Where, Group, By, Having, Order, Limit, Offset,
    Asc, Desc, Nulls, First, Last,
    Join, Inner, Left, Right, Full, Cross, Outer, On, Using,
    Union, Intersect, Except,
    Insert, Into, Values,
    Update, Set,
    Delete,
    Create, Alter, Drop,
    Table, Index, View, Schema,
    Primary, Key, Unique, References, Check, Default, Constraint,
    Begin, Commit, Rollback,
    Returning,
    Case, When, Then, Else, End,
    Cast, Exists,
    Count, Sum, Avg, Min, Max,

    // Operators and Punctuation
    Plus, Minus, Star, Slash, Percent,
    Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
    LeftParen, RightParen, LeftBracket, RightBracket,
    Comma, Dot, Semicolon, Colon, DoubleColon,
    Concat // ||
};

std::string_view to_string(TokenType type);

} // namespace nemo::sql

#endif // NEMO_SQL_TOKEN_TYPE_HPP
