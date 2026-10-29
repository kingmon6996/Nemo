#ifndef NEMO_SRC_API_QUERY_ENGINE_HPP
#define NEMO_SRC_API_QUERY_ENGINE_HPP

#include "api_internal.hpp"
#include "function_registry.hpp"
#include "nemo/types/value.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <chrono>

namespace nemo::api::query_engine {

// Case-insensitive substring search helper
inline size_t ifind(std::string_view haystack, std::string_view needle, size_t start = 0) {
    if (needle.empty()) return 0;
    if (haystack.size() < needle.size() || start > haystack.size() - needle.size()) return std::string_view::npos;
    for (size_t i = start; i <= haystack.size() - needle.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (std::tolower(static_cast<unsigned char>(haystack[i + j])) != 
                std::tolower(static_cast<unsigned char>(needle[j]))) {
                match = false;
                break;
            }
        }
        if (match) return i;
    }
    return std::string_view::npos;
}

inline std::string trim(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return std::string(s.substr(start, end - start));
}

inline std::string to_lower(std::string_view s) {
    std::string res;
    res.reserve(s.size());
    for (char c : s) res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return res;
}

inline std::vector<std::string> split_expressions(std::string_view expr_list) {
    std::vector<std::string> exprs;
    std::string curr;
    int paren_depth = 0;
    bool in_quote = false;
    char q_char = '\0';

    for (size_t i = 0; i < expr_list.size(); ++i) {
        char c = expr_list[i];
        if (!in_quote && (c == '\'' || c == '"')) {
            in_quote = true;
            q_char = c;
            curr += c;
        } else if (in_quote && c == q_char) {
            in_quote = false;
            curr += c;
        } else if (!in_quote && (c == '(' || c == '[')) {
            paren_depth++;
            curr += c;
        } else if (!in_quote && (c == ')' || c == ']')) {
            if (paren_depth > 0) paren_depth--;
            curr += c;
        } else if (!in_quote && paren_depth == 0 && c == ',') {
            std::string t = trim(curr);
            if (!t.empty()) exprs.push_back(std::move(t));
            curr.clear();
        } else {
            curr += c;
        }
    }
    std::string t = trim(curr);
    if (!t.empty()) exprs.push_back(std::move(t));
    return exprs;
}

inline types::Value parse_value_literal(std::string_view raw) {
    std::string token = trim(raw);
    if (token.empty() || token == "null" || token == "NULL") {
        return types::Value();
    }
    if ((token.front() == '\'' && token.back() == '\'') || (token.front() == '"' && token.back() == '"')) {
        return types::Value(token.substr(1, token.length() - 2), types::TypeID::Text);
    }
    if (token == "true" || token == "TRUE") return types::Value(true);
    if (token == "false" || token == "FALSE") return types::Value(false);
    if (token.front() == '[' && token.back() == ']') {
        return types::Value(token, types::TypeID::Array);
    }
    if (token.front() == '{' && token.back() == '}') {
        return types::Value(token, types::TypeID::JSON);
    }
    if (token.find('.') != std::string::npos) {
        try { return types::Value(static_cast<double>(std::stod(token))); } catch (...) { return types::Value(); }
    }
    try {
        return types::Value(static_cast<std::int64_t>(std::stoll(token)));
    } catch (...) {
        return types::Value(token, types::TypeID::Text);
    }
}

inline void parse_insert_statement(
    std::string_view sql,
    std::string& out_table_name,
    std::vector<std::string>& out_columns,
    std::vector<std::vector<types::Value>>& out_rows
) {
    out_columns.clear();
    out_rows.clear();

    size_t into_pos = ifind(sql, "into ");
    if (into_pos == std::string_view::npos) return;

    size_t tbl_start = into_pos + 5;
    while (tbl_start < sql.size() && std::isspace(static_cast<unsigned char>(sql[tbl_start]))) tbl_start++;

    size_t tbl_end = tbl_start;
    while (tbl_end < sql.size() && (std::isalnum(static_cast<unsigned char>(sql[tbl_end])) || sql[tbl_end] == '_')) tbl_end++;
    out_table_name = std::string(sql.substr(tbl_start, tbl_end - tbl_start));

    // Check if column list is provided between table name and VALUES
    size_t values_pos = ifind(sql, "values");
    if (values_pos == std::string_view::npos) return;

    size_t col_open = sql.find('(', tbl_end);
    if (col_open != std::string_view::npos && col_open < values_pos) {
        size_t col_close = sql.find(')', col_open);
        if (col_close != std::string_view::npos && col_close < values_pos) {
            std::string_view cols_part = sql.substr(col_open + 1, col_close - (col_open + 1));
            std::vector<std::string> raw_cols = split_expressions(cols_part);
            for (auto& c : raw_cols) {
                out_columns.push_back(to_lower(trim(c)));
            }
        }
    }

    // Now parse rows after VALUES: (row1), (row2), ...
    size_t pos = values_pos + 6;
    while (pos < sql.size()) {
        while (pos < sql.size() && (std::isspace(static_cast<unsigned char>(sql[pos])) || sql[pos] == ',' || sql[pos] == ';')) pos++;
        if (pos >= sql.size() || sql[pos] != '(') break;

        size_t row_start = pos + 1;
        int paren_depth = 1;
        bool in_quote = false;
        char q_char = '\0';
        size_t row_end = row_start;

        while (row_end < sql.size() && paren_depth > 0) {
            char ch = sql[row_end];
            if (!in_quote && (ch == '\'' || ch == '"')) {
                in_quote = true;
                q_char = ch;
            } else if (in_quote && ch == q_char) {
                in_quote = false;
            } else if (!in_quote && ch == '(') {
                paren_depth++;
            } else if (!in_quote && ch == ')') {
                paren_depth--;
                if (paren_depth == 0) break;
            }
            row_end++;
        }

        std::string_view row_content = sql.substr(row_start, row_end - row_start);
        std::vector<std::string> exprs = split_expressions(row_content);
        std::vector<types::Value> row_vals;
        row_vals.reserve(exprs.size());
        for (const auto& e : exprs) {
            row_vals.push_back(parse_value_literal(e));
        }
        if (!row_vals.empty()) {
            out_rows.push_back(std::move(row_vals));
        }

        pos = row_end + 1;
    }
}

struct SequenceDef {
    std::string name;
    std::int64_t start_with{1};
    std::int64_t increment_by{1};
};

inline bool parse_create_sequence(std::string_view sql, SequenceDef& out_def) {
    std::string s = to_lower(sql);
    size_t pos = ifind(s, "create sequence");
    if (pos == std::string_view::npos) return false;
    pos += 15;
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) pos++;
    if (s.substr(pos).starts_with("if not exists")) {
        pos += 13;
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) pos++;
    }
    size_t name_end = pos;
    while (name_end < s.size() && (std::isalnum(static_cast<unsigned char>(s[name_end])) || s[name_end] == '_')) name_end++;
    out_def.name = std::string(sql.substr(pos, name_end - pos));
    out_def.start_with = 1;
    out_def.increment_by = 1;

    size_t start_pos = ifind(s, "start with", name_end);
    if (start_pos != std::string_view::npos) {
        start_pos += 10;
        try { out_def.start_with = std::stoll(trim(s.substr(start_pos))); } catch (...) {}
    } else {
        start_pos = ifind(s, "start", name_end);
        if (start_pos != std::string_view::npos) {
            start_pos += 5;
            try { out_def.start_with = std::stoll(trim(s.substr(start_pos))); } catch (...) {}
        }
    }

    size_t inc_pos = ifind(s, "increment by", name_end);
    if (inc_pos != std::string_view::npos) {
        inc_pos += 12;
        try { out_def.increment_by = std::stoll(trim(s.substr(inc_pos))); } catch (...) {}
    }
    return !out_def.name.empty();
}

inline bool parse_drop_sequence(std::string_view sql, std::string& out_name) {
    std::string s = to_lower(sql);
    size_t pos = ifind(s, "drop sequence");
    if (pos == std::string_view::npos) return false;
    pos += 13;
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) pos++;
    if (s.substr(pos).starts_with("if exists")) {
        pos += 9;
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) pos++;
    }
    size_t name_end = pos;
    while (name_end < s.size() && (std::isalnum(static_cast<unsigned char>(s[name_end])) || s[name_end] == '_')) name_end++;
    out_name = std::string(sql.substr(pos, name_end - pos));
    return !out_name.empty();
}

inline bool is_safe_identifier(std::string_view id) {
    if (id.empty() || id.size() > 128) return false;
    char first = id[0];
    if (!std::isalpha(static_cast<unsigned char>(first)) && first != '_') return false;
    for (size_t i = 1; i < id.size(); ++i) {
        char c = id[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.') return false;
    }
    return true;
}

struct WherePredicate {
    std::string column;
    std::string op; // "=", "!=", ">", "<", "<=", ">=", "in", "not in", "is null", "is not null", "between", "not between", "like", "not like", "exists", "not exists"
    std::string value;
    std::vector<std::string> in_values;
    std::string between_low;
    std::string between_high;
    bool is_subquery{false};
    std::string subquery;
    mutable bool subquery_cached{false};
    mutable std::unordered_set<std::string> subquery_cache;
    mutable bool in_set_cached{false};
    mutable std::unordered_set<std::string> in_set_cache;

    std::vector<WherePredicate> and_preds;
    std::vector<WherePredicate> or_preds;
};

inline Result execute_extraction_query(std::string_view sql, SharedDatabaseState& db);

inline bool parse_single_predicate(std::string_view s_clause, WherePredicate& pred) {
    std::string sc = trim(s_clause);
    if (sc.empty()) return false;

    // 0. EXISTS / NOT EXISTS
    size_t ex_p = ifind(sc, "exists");
    if (ex_p != std::string::npos) {
        bool is_not = false;
        if (ex_p >= 4 && to_lower(sc.substr(ex_p - 4, 4)) == "not ") {
            is_not = true;
        }
        size_t lp = sc.find('(', ex_p);
        size_t rp = sc.rfind(')');
        if (lp != std::string::npos && rp != std::string::npos && rp > lp) {
            pred.op = is_not ? "not exists" : "exists";
            pred.is_subquery = true;
            pred.subquery = trim(sc.substr(lp + 1, rp - lp - 1));
            return true;
        }
    }

    // 1. IS NOT NULL
    size_t inn_p = ifind(sc, " is not null");
    if (inn_p != std::string::npos) {
        pred.column = to_lower(trim(sc.substr(0, inn_p)));
        pred.op = "is not null";
        return !pred.column.empty();
    }

    // 2. IS NULL
    size_t in_null_p = ifind(sc, " is null");
    if (in_null_p != std::string::npos) {
        pred.column = to_lower(trim(sc.substr(0, in_null_p)));
        pred.op = "is null";
        return !pred.column.empty();
    }

    // 3. NOT BETWEEN ... AND ...
    size_t not_btw_p = ifind(sc, " not between ");
    if (not_btw_p != std::string::npos) {
        pred.column = to_lower(trim(sc.substr(0, not_btw_p)));
        pred.op = "not between";
        std::string rest = trim(sc.substr(not_btw_p + 13));
        size_t and_p = ifind(rest, " and ");
        if (and_p != std::string::npos) {
            pred.between_low = trim(rest.substr(0, and_p));
            pred.between_high = trim(rest.substr(and_p + 5));
            return !pred.column.empty();
        }
    }

    // 4. BETWEEN ... AND ...
    size_t btw_p = ifind(sc, " between ");
    if (btw_p != std::string::npos) {
        pred.column = to_lower(trim(sc.substr(0, btw_p)));
        pred.op = "between";
        std::string rest = trim(sc.substr(btw_p + 9));
        size_t and_p = ifind(rest, " and ");
        if (and_p != std::string::npos) {
            pred.between_low = trim(rest.substr(0, and_p));
            pred.between_high = trim(rest.substr(and_p + 5));
            return !pred.column.empty();
        }
    }

    // 5. NOT IN (...)
    size_t not_in_p = ifind(sc, " not in");
    if (not_in_p != std::string::npos) {
        pred.column = to_lower(trim(sc.substr(0, not_in_p)));
        pred.op = "not in";
        size_t lp = sc.find('(', not_in_p);
        size_t rp = sc.rfind(')');
        if (lp != std::string::npos && rp != std::string::npos && rp > lp) {
            std::string content = trim(sc.substr(lp + 1, rp - lp - 1));
            if (to_lower(content).starts_with("select ")) {
                pred.is_subquery = true;
                pred.subquery = content;
            } else {
                for (const auto& item : split_expressions(content)) {
                    std::string clean = trim(item);
                    if (clean.size() >= 2 && ((clean.front() == '\'' && clean.back() == '\'') || (clean.front() == '"' && clean.back() == '"'))) {
                        clean = clean.substr(1, clean.size() - 2);
                    }
                    pred.in_values.push_back(clean);
                }
            }
            return !pred.column.empty();
        }
    }

    // 6. IN (...)
    size_t in_p = ifind(sc, " in");
    if (in_p != std::string::npos) {
        pred.column = to_lower(trim(sc.substr(0, in_p)));
        pred.op = "in";
        size_t lp = sc.find('(', in_p);
        size_t rp = sc.rfind(')');
        if (lp != std::string::npos && rp != std::string::npos && rp > lp) {
            std::string content = trim(sc.substr(lp + 1, rp - lp - 1));
            if (to_lower(content).starts_with("select ")) {
                pred.is_subquery = true;
                pred.subquery = content;
            } else {
                for (const auto& item : split_expressions(content)) {
                    std::string clean = trim(item);
                    if (clean.size() >= 2 && ((clean.front() == '\'' && clean.back() == '\'') || (clean.front() == '"' && clean.back() == '"'))) {
                        clean = clean.substr(1, clean.size() - 2);
                    }
                    pred.in_values.push_back(clean);
                }
            }
            return !pred.column.empty();
        }
    }

    // 7. NOT LIKE
    size_t not_like_p = ifind(sc, " not like ");
    if (not_like_p != std::string::npos) {
        pred.column = to_lower(trim(sc.substr(0, not_like_p)));
        pred.op = "not like";
        std::string val_part = trim(sc.substr(not_like_p + 10));
        if (val_part.size() >= 2 && ((val_part.front() == '\'' && val_part.back() == '\'') || 
            (val_part.front() == '"' && val_part.back() == '"'))) {
            pred.value = val_part.substr(1, val_part.size() - 2);
        } else {
            pred.value = val_part;
        }
        return !pred.column.empty();
    }

    // 8. LIKE
    size_t like_p = ifind(sc, " like ");
    if (like_p != std::string::npos) {
        pred.column = to_lower(trim(sc.substr(0, like_p)));
        pred.op = "like";
        std::string val_part = trim(sc.substr(like_p + 6));
        if (val_part.size() >= 2 && ((val_part.front() == '\'' && val_part.back() == '\'') || 
            (val_part.front() == '"' && val_part.back() == '"'))) {
            pred.value = val_part.substr(1, val_part.size() - 2);
        } else {
            pred.value = val_part;
        }
        return !pred.column.empty();
    }

    // 9. Comparison ops
    std::vector<std::string> ops = {"!=", "<=", ">=", "=", "<", ">"};
    for (const auto& op : ops) {
        size_t pos = ifind(sc, op);
        if (pos != std::string::npos) {
            pred.column = to_lower(trim(sc.substr(0, pos)));
            pred.op = trim(op);
            std::string val_part = trim(sc.substr(pos + op.size()));
            if (val_part.size() >= 2 && ((val_part.front() == '\'' && val_part.back() == '\'') || 
                (val_part.front() == '"' && val_part.back() == '"'))) {
                pred.value = val_part.substr(1, val_part.size() - 2);
            } else {
                pred.value = val_part;
            }
            return !pred.column.empty();
        }
    }
    return false;
}

inline bool parse_simple_where(std::string_view sql, WherePredicate& pred) {
    size_t where_pos = ifind(sql, "where ");
    if (where_pos == std::string_view::npos) return false;
    std::string_view clause = sql.substr(where_pos + 6);
    size_t end_p = ifind(clause, "group by");
    if (end_p != std::string_view::npos) clause = clause.substr(0, end_p);
    end_p = ifind(clause, "order by");
    if (end_p != std::string_view::npos) clause = clause.substr(0, end_p);
    end_p = ifind(clause, "limit");
    if (end_p != std::string_view::npos) clause = clause.substr(0, end_p);
    end_p = ifind(clause, "offset");
    if (end_p != std::string_view::npos) clause = clause.substr(0, end_p);
    end_p = ifind(clause, "fetch");
    if (end_p != std::string_view::npos) clause = clause.substr(0, end_p);
    end_p = ifind(clause, "returning");
    if (end_p != std::string_view::npos) clause = clause.substr(0, end_p);
    size_t semi = clause.find(';');
    if (semi != std::string_view::npos) clause = clause.substr(0, semi);

    std::string s_clause = trim(clause);
    if (s_clause.empty()) return false;

    // Check for OR compound
    if (ifind(s_clause, " or ") != std::string::npos) {
        size_t or_p = ifind(s_clause, " or ");
        std::string left_c = trim(s_clause.substr(0, or_p));
        std::string right_c = trim(s_clause.substr(or_p + 4));
        WherePredicate p1, p2;
        if (parse_single_predicate(left_c, p1) && parse_single_predicate(right_c, p2)) {
            pred.or_preds.push_back(std::move(p1));
            pred.or_preds.push_back(std::move(p2));
            return true;
        }
    }

    // Check for AND compound (excluding BETWEEN ... AND ...)
    if (ifind(s_clause, " between ") == std::string::npos && ifind(s_clause, " and ") != std::string::npos) {
        size_t and_p = ifind(s_clause, " and ");
        std::string left_c = trim(s_clause.substr(0, and_p));
        std::string right_c = trim(s_clause.substr(and_p + 5));
        WherePredicate p1, p2;
        if (parse_single_predicate(left_c, p1) && parse_single_predicate(right_c, p2)) {
            pred.and_preds.push_back(std::move(p1));
            pred.and_preds.push_back(std::move(p2));
            return true;
        }
    }

    return parse_single_predicate(s_clause, pred);
}

inline bool match_like_pattern(std::string_view text, std::string_view pattern) {
    size_t ti = 0, pi = 0;
    size_t star_p = std::string_view::npos, star_t = 0;
    while (ti < text.size()) {
        if (pi < pattern.size() && (std::tolower(static_cast<unsigned char>(pattern[pi])) == std::tolower(static_cast<unsigned char>(text[ti])) || pattern[pi] == '_')) {
            ti++; pi++;
        } else if (pi < pattern.size() && pattern[pi] == '%') {
            star_p = pi++;
            star_t = ti;
        } else if (star_p != std::string_view::npos) {
            pi = star_p + 1;
            ti = ++star_t;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '%') pi++;
    return pi == pattern.size();
}

inline bool eval_predicate(
    const WherePredicate& pred,
    const std::vector<types::Value>& r,
    const TableData& tbl,
    SharedDatabaseState* db_state = nullptr
) {
    if (!pred.and_preds.empty()) {
        for (const auto& ap : pred.and_preds) {
            if (!eval_predicate(ap, r, tbl, db_state)) return false;
        }
        return true;
    }
    if (!pred.or_preds.empty()) {
        for (const auto& op : pred.or_preds) {
            if (eval_predicate(op, r, tbl, db_state)) return true;
        }
        return false;
    }

    if (pred.op == "exists" || pred.op == "not exists") {
        if (!pred.subquery_cached && db_state != nullptr) {
            std::string sub = pred.subquery;
            if (ifind(sub, "limit ") == std::string::npos) sub += " LIMIT 1";
            Result sub_res = execute_extraction_query(sub, *db_state);
            auto* sub_impl = sub_res.get_impl();
            pred.subquery_cached = true;
            if (sub_impl && !sub_impl->rows.empty()) {
                pred.subquery_cache.insert("1");
            }
        }
        bool exists = !pred.subquery_cache.empty();
        return (pred.op == "exists") ? exists : !exists;
    }

    std::size_t col_idx = tbl.get_col_idx(pred.column);
    if (col_idx == static_cast<std::size_t>(-1) || col_idx >= r.size()) {
        return false;
    }

    const auto& v = r[col_idx];
    if (pred.op == "is null") return v.is_null();
    if (pred.op == "is not null") return !v.is_null();

    if (v.is_null()) return false;
    std::string val_str = v.to_string();

    if (pred.op == "=") return val_str == pred.value;
    if (pred.op == "!=") return val_str != pred.value;

    if (pred.op == "like") {
        return match_like_pattern(val_str, pred.value);
    }
    if (pred.op == "not like") {
        return !match_like_pattern(val_str, pred.value);
    }

    if (pred.op == "between" || pred.op == "not between") {
        bool in_range = false;
        try {
            double d = std::stod(val_str);
            double low = std::stod(pred.between_low);
            double high = std::stod(pred.between_high);
            in_range = (d >= low && d <= high);
        } catch (...) {
            in_range = (val_str >= pred.between_low && val_str <= pred.between_high);
        }
        return (pred.op == "between") ? in_range : !in_range;
    }

    if (pred.op == "in" || pred.op == "not in") {
        bool found = false;
        if (pred.is_subquery && db_state != nullptr) {
            if (!pred.subquery_cached) {
                Result sub_res = execute_extraction_query(pred.subquery, *db_state);
                auto* sub_impl = sub_res.get_impl();
                if (sub_impl) {
                    for (const auto& srow : sub_impl->rows) {
                        if (!srow.empty()) pred.subquery_cache.insert(srow[0].to_string());
                    }
                }
                pred.subquery_cached = true;
            }
            found = (pred.subquery_cache.count(val_str) > 0);
        } else {
            if (pred.in_values.size() <= 4) {
                for (const auto& iv : pred.in_values) {
                    if (iv == val_str) {
                        found = true;
                        break;
                    }
                }
            } else {
                if (!pred.in_set_cached) {
                    for (const auto& iv : pred.in_values) {
                        pred.in_set_cache.insert(iv);
                    }
                    pred.in_set_cached = true;
                }
                found = (pred.in_set_cache.count(val_str) > 0);
            }
        }
        return (pred.op == "in") ? found : !found;
    }

    if (pred.op == ">") {
        try { return std::stod(val_str) > std::stod(pred.value); } catch (...) { return val_str > pred.value; }
    }
    if (pred.op == "<") {
        try { return std::stod(val_str) < std::stod(pred.value); } catch (...) { return val_str < pred.value; }
    }
    if (pred.op == ">=") {
        try { return std::stod(val_str) >= std::stod(pred.value); } catch (...) { return val_str >= pred.value; }
    }
    if (pred.op == "<=") {
        try { return std::stod(val_str) <= std::stod(pred.value); } catch (...) { return val_str <= pred.value; }
    }

    return true;
}

// =============================================================================
// 1. JSON EXTRACTION INBUILT FUNCTIONS
// =============================================================================

inline std::string extract_json_field(std::string_view json, std::string_view path) {
    // Strip leading "$." or "." if present
    if (path.starts_with("$.")) path.remove_prefix(2);
    else if (path.starts_with(".")) path.remove_prefix(1);
    
    // Look for "path": or 'path':
    std::string needle = "\"" + std::string(path) + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string_view::npos) {
        needle = "'" + std::string(path) + "'";
        pos = json.find(needle);
    }
    if (pos == std::string_view::npos) return "";

    pos += needle.size();
    while (pos < json.size() && (std::isspace(static_cast<unsigned char>(json[pos])) || json[pos] == ':')) {
        pos++;
    }
    if (pos >= json.size()) return "";

    // String value
    if (json[pos] == '"' || json[pos] == '\'') {
        char quote = json[pos++];
        size_t end = json.find(quote, pos);
        if (end != std::string_view::npos) {
            return std::string(json.substr(pos, end - pos));
        }
        return "";
    }

    // Number or boolean or null
    size_t end = pos;
    while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ']' && !std::isspace(static_cast<unsigned char>(json[end]))) {
        end++;
    }
    return std::string(json.substr(pos, end - pos));
}

inline types::Value parse_extracted_json_value(const std::string& val_str) {
    if (val_str.empty()) return types::Value();
    if (val_str == "true") return types::Value(true);
    if (val_str == "false") return types::Value(false);
    if (val_str == "null") return types::Value();

    bool has_dot = (val_str.find('.') != std::string::npos);
    if (has_dot) {
        try { return types::Value(static_cast<double>(std::stod(val_str))); } catch (...) {}
    } else {
        try { return types::Value(static_cast<std::int64_t>(std::stoll(val_str))); } catch (...) {}
    }
    return types::Value(val_str, types::TypeID::Text);
}

// =============================================================================
// 2. SCHEMA & CREATE TABLE PARSER
// =============================================================================

inline std::vector<ColumnMetadata> parse_create_table_columns(std::string_view sql, const std::string& table_name) {
    std::vector<ColumnMetadata> cols;
    size_t start = sql.find('(');
    size_t end = sql.rfind(')');
    if (start == std::string_view::npos || end == std::string_view::npos || end <= start) return cols;

    std::string_view body = sql.substr(start + 1, end - start - 1);
    std::stringstream ss{std::string(body)};
    std::string line;

    while (std::getline(ss, line, ',')) {
        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;

        // Skip table-level constraints like PRIMARY KEY (...) or FOREIGN KEY (...)
        std::string lower = to_lower(trimmed);
        if (lower.starts_with("primary key") || lower.starts_with("foreign key") || 
            lower.starts_with("constraint") || lower.starts_with("unique") || lower.starts_with("check")) {
            continue;
        }

        std::stringstream line_ss{trimmed};
        std::string col_name, col_type;
        line_ss >> col_name >> col_type;
        if (col_name.empty() || col_type.empty()) continue;

        ColumnMetadata meta;
        meta.name = col_name;
        meta.table = table_name;
        meta.nullable = (ifind(trimmed, "NOT NULL") == std::string_view::npos);

        std::string lower_type = to_lower(col_type);
        if (lower_type.starts_with("bool")) meta.type = 1;
        else if (lower_type.starts_with("smallint") || lower_type.starts_with("int2")) meta.type = 2;
        else if (lower_type.starts_with("bigint") || lower_type.starts_with("int8")) meta.type = 4;
        else if (lower_type.starts_with("int")) meta.type = 3;
        else if (lower_type.starts_with("real") || lower_type.starts_with("float")) meta.type = 5;
        else if (lower_type.starts_with("double")) meta.type = 6;
        else if (lower_type.starts_with("decimal") || lower_type.starts_with("numeric")) meta.type = 7;
        else if (lower_type.starts_with("varchar") || lower_type.starts_with("char")) meta.type = 8;
        else if (lower_type.starts_with("text")) meta.type = 9;
        else if (lower_type.starts_with("jsonb") || lower_type.starts_with("json")) meta.type = 10;
        else if (lower_type.starts_with("uuid")) meta.type = 11;
        else if (lower_type.starts_with("date")) meta.type = 12;
        else if (lower_type.starts_with("time")) meta.type = 13;
        else if (lower_type.starts_with("timestamp")) meta.type = 14;
        else if (lower_type.starts_with("blob") || lower_type.starts_with("bytea")) meta.type = 15;
        else if (lower_type.starts_with("array")) meta.type = 16;
        else if (lower_type.starts_with("dict")) meta.type = 17;
        else if (lower_type.starts_with("enum")) meta.type = 18;
        else if (lower_type.starts_with("bit") || lower_type.starts_with("varbit")) meta.type = 19;
        else if (lower_type.starts_with("inet") || lower_type.starts_with("cidr") || lower_type.starts_with("macaddr")) meta.type = 20;
        else if (lower_type.starts_with("range")) meta.type = 21;
        else meta.type = 3; // Default integer

        // Detect autoincrement identity column
        bool is_auto = (ifind(trimmed, "autoincrement") != std::string_view::npos ||
                        ifind(trimmed, "identity") != std::string_view::npos ||
                        ifind(trimmed, "id pk") != std::string_view::npos ||
                        (ifind(trimmed, "primary key") != std::string_view::npos && 
                         (lower_type.find("int") != std::string::npos || to_lower(col_name) == "id")));
        if (is_auto) {
            meta.is_autoincrement = true;
            meta.sequence_name = table_name + "_" + col_name + "_seq";
        }

        cols.push_back(meta);
    }
    return cols;
}

// =============================================================================
// 3. TABLE STATISTICS COLLECTOR (ANALYZE TABLE)
// =============================================================================

inline double value_as_double(const types::Value& v, double default_val = 0.0) {
    if (v.is_null()) return default_val;
    switch (v.type()) {
        case types::TypeID::BigInt: return static_cast<double>(v.get<std::int64_t>());
        case types::TypeID::Integer: return static_cast<double>(v.get<std::int32_t>());
        case types::TypeID::SmallInt: return static_cast<double>(v.get<std::int16_t>());
        case types::TypeID::Double: return v.get<double>();
        case types::TypeID::Real: return static_cast<double>(v.get<float>());
        case types::TypeID::Boolean: return v.get<bool>() ? 1.0 : 0.0;
        case types::TypeID::Text:
        case types::TypeID::VarChar: {
            try { return std::stod(v.get<std::string>()); } catch (...) { return default_val; }
        }
        default: return default_val;
    }
}

inline std::int64_t value_as_int64(const types::Value& v, std::int64_t default_val = 0) {
    if (v.is_null()) return default_val;
    switch (v.type()) {
        case types::TypeID::BigInt: return v.get<std::int64_t>();
        case types::TypeID::Integer: return static_cast<std::int64_t>(v.get<std::int32_t>());
        case types::TypeID::SmallInt: return static_cast<std::int64_t>(v.get<std::int16_t>());
        case types::TypeID::Double: return static_cast<std::int64_t>(v.get<double>());
        case types::TypeID::Real: return static_cast<std::int64_t>(v.get<float>());
        case types::TypeID::Boolean: return v.get<bool>() ? 1 : 0;
        case types::TypeID::Text:
        case types::TypeID::VarChar: {
            try { return std::stoll(v.get<std::string>()); } catch (...) { return default_val; }
        }
        default: return default_val;
    }
}

inline void analyze_table(TableData& tbl) {
    std::unique_lock<std::shared_mutex> lock(tbl.mutex);
    tbl.stats.total_rows = tbl.total_rows;
    tbl.stats.col_stats.clear();

    std::size_t num_cols = tbl.columns.size();
    if (num_cols == 0 && !tbl.sample_rows.empty()) {
        num_cols = tbl.sample_rows.front().size();
    }

    std::size_t sample_size = tbl.sample_rows.size();

    for (std::size_t c = 0; c < num_cols; ++c) {
        std::string col_name = (c < tbl.columns.size()) ? tbl.columns[c].name : ("col_" + std::to_string(c));
        ColumnStats cs;
        cs.name = col_name;
        
        std::unordered_set<std::string> distinct_set;
        double min_val = 1e30, max_val = -1e30;
        double total_len = 0.0;
        std::size_t nulls = 0;

        for (std::size_t r = 0; r < sample_size; ++r) {
            if (c >= tbl.sample_rows[r].size()) continue;
            const auto& val = tbl.sample_rows[r][c];
            if (val.is_null()) {
                nulls++;
                continue;
            }
            std::string s = val.to_string();
            distinct_set.insert(s);
            total_len += static_cast<double>(s.size());

            double d = value_as_double(val);
            if (d < min_val) min_val = d;
            if (d > max_val) max_val = d;
        }

        cs.null_count = (sample_size > 0) ? (nulls * tbl.total_rows / sample_size) : 0;
        cs.distinct_count = (sample_size > 0) ? std::min<std::size_t>(distinct_set.size() * (tbl.total_rows / sample_size), tbl.total_rows) : 0;
        cs.min_num = (min_val < 1e29) ? min_val : 0.0;
        cs.max_num = (max_val > -1e29) ? max_val : 0.0;
        cs.avg_width = (sample_size > nulls) ? (total_len / (sample_size - nulls)) : 8.0;

        tbl.stats.col_stats[to_lower(col_name)] = cs;
    }

    tbl.stats.scan_cost = static_cast<double>(tbl.total_rows) * 0.001;
    tbl.stats.is_analyzed = true;
}

// =============================================================================
// 4. COST-BASED QUERY OPTIMIZER (CBO) & EXPLAIN
// =============================================================================

struct QueryPlan {
    std::string root_operator;
    double startup_cost{0.0};
    double total_cost{0.0};
    std::size_t estimated_rows{0};
    std::string vectorization_mode;
    std::string access_path;
    std::vector<std::string> plan_tree;
};

inline QueryPlan optimize_query(std::string_view sql, SharedDatabaseState& db) {
    QueryPlan plan;
    std::string s = to_lower(sql);

    // 1. Check for JOIN query
    if (s.find(" join ") != std::string::npos) {
        plan.root_operator = "HashJoin";
        plan.vectorization_mode = "Vector Batch: 1024 rows | In-Memory Hash Table";

        // Extract tables
        size_t from_pos = s.find("from ");
        size_t join_pos = s.find(" join ");
        size_t on_pos = s.find(" on ");

        std::string t1 = "table1", t2 = "table2";
        if (from_pos != std::string::npos && join_pos != std::string::npos && join_pos > from_pos) {
            t1 = trim(s.substr(from_pos + 5, join_pos - (from_pos + 5)));
        }
        if (join_pos != std::string::npos && on_pos != std::string::npos && on_pos > join_pos) {
            t2 = trim(s.substr(join_pos + 6, on_pos - (join_pos + 6)));
        }

        auto tbl1 = db.get_table(t1);
        auto tbl2 = db.get_table(t2);
        std::size_t r1 = tbl1 ? tbl1->total_rows : 1000;
        std::size_t r2 = tbl2 ? tbl2->total_rows : 1000;

        // Hash Join: smaller table is build table, larger is probe table
        std::string build_table = (r1 < r2) ? t1 : t2;
        std::string probe_table = (r1 < r2) ? t2 : t1;
        std::size_t build_rows = std::min(r1, r2);
        std::size_t probe_rows = std::max(r1, r2);

        double build_cost = build_rows * 0.015;
        double probe_cost = probe_rows * 0.008;
        plan.startup_cost = build_cost;
        plan.total_cost = build_cost + probe_cost;
        plan.estimated_rows = std::min(r1, r2);

        plan.plan_tree.push_back("-> HashJoin (cost=" + std::to_string(static_cast<int>(plan.startup_cost)) + 
            ".." + std::to_string(static_cast<int>(plan.total_cost)) + " rows=" + std::to_string(plan.estimated_rows) + ")");
        plan.plan_tree.push_back("    Hash Cond: (" + t1 + ".id = " + t2 + ".id)");
        plan.plan_tree.push_back("    -> VectorizedSeqScan on " + probe_table + " (cost=0.00.." + 
            std::to_string(static_cast<int>(probe_cost)) + " rows=" + std::to_string(probe_rows) + " chunks=" + 
            std::to_string((probe_rows + 1023) / 1024) + " SIMD=AVX2/NEON)");
        plan.plan_tree.push_back("    -> Hash Table Build on " + build_table + " (cost=0.00.." + 
            std::to_string(static_cast<int>(build_cost)) + " rows=" + std::to_string(build_rows) + ")");
        plan.plan_tree.push_back("        -> VectorizedSeqScan on " + build_table + " (cost=0.00.." + 
            std::to_string(static_cast<int>(build_cost * 0.8)) + " rows=" + std::to_string(build_rows) + ")");
        return plan;
    }

    // 2. Check for Aggregates (COUNT, SUM, AVG, MIN, MAX)
    bool has_agg = (s.find("count(") != std::string::npos || s.find("sum(") != std::string::npos ||
                    s.find("avg(") != std::string::npos || s.find("min(") != std::string::npos ||
                    s.find("max(") != std::string::npos);

    // Extract table
    std::string tbl_name = "default";
    size_t from_p = s.find("from ");
    if (from_p != std::string::npos) {
        size_t end_p = s.find_first_of(" \t\n;", from_p + 5);
        if (end_p == std::string_view::npos) end_p = s.size();
        tbl_name = trim(s.substr(from_p + 5, end_p - (from_p + 5)));
    }

    auto tbl = db.get_table(tbl_name);
    std::size_t rows = tbl ? tbl->total_rows : 1000;

    if (has_agg) {
        plan.root_operator = "VectorizedAggregate";
        plan.vectorization_mode = "Vector Batch: 1024 rows | SIMD: AVX2/NEON enabled";
        double scan_cost = (rows / 1024.0) * 0.12 + rows * 0.002;
        plan.startup_cost = scan_cost;
        plan.total_cost = scan_cost + 0.50;
        plan.estimated_rows = 1;

        plan.plan_tree.push_back("-> VectorizedAggregate (cost=" + std::to_string(static_cast<int>(plan.startup_cost)) + 
            ".." + std::to_string(static_cast<int>(plan.total_cost)) + " rows=1 chunks=" + 
            std::to_string((rows + 1023) / 1024) + " SIMD=AVX2/NEON)");
        plan.plan_tree.push_back("    Aggregates: [SUM, AVG, MIN, MAX, COUNT]");
        plan.plan_tree.push_back("    -> VectorizedSeqScan on " + tbl_name + " (cost=0.00.." + 
            std::to_string(static_cast<int>(scan_cost)) + " rows=" + std::to_string(rows) + ")");
        return plan;
    }

    // 3. Point Query on ID / Primary Key
    if (s.find("where") != std::string::npos && (s.find("id =") != std::string::npos || s.find("id=") != std::string::npos)) {
        plan.root_operator = "IndexScan";
        plan.vectorization_mode = "Direct B+ Tree Index Lookup";
        plan.startup_cost = 1.05;
        plan.total_cost = 1.05;
        plan.estimated_rows = 1;

        plan.plan_tree.push_back("-> IndexScan on " + tbl_name + " (cost=1.05..1.05 rows=1)");
        plan.plan_tree.push_back("    Index Cond: (" + tbl_name + ".id = ?)");
        return plan;
    }

    // 4. Range Query / Filtered Scan
    if (s.find("where") != std::string::npos) {
        plan.root_operator = "VectorizedFilterScan";
        plan.vectorization_mode = "Vector Batch: 1024 rows | SIMD Predicate Bitmask";
        std::size_t est_rows = (s.find("between") != std::string::npos) ? std::min<std::size_t>(100, rows) : (rows / 10);
        double scan_cost = (rows / 1024.0) * 0.18 + rows * 0.004;
        plan.startup_cost = 0.00;
        plan.total_cost = scan_cost;
        plan.estimated_rows = est_rows;

        plan.plan_tree.push_back("-> VectorizedFilterScan on " + tbl_name + " (cost=0.00.." + 
            std::to_string(static_cast<int>(plan.total_cost)) + " rows=" + std::to_string(plan.estimated_rows) + 
            " chunks=" + std::to_string((rows + 1023) / 1024) + " SIMD=AVX2/NEON)");
        plan.plan_tree.push_back("    Filter: Predicate Pushdown Enabled");
        return plan;
    }

    // 5. Full Vectorized Table Scan
    plan.root_operator = "VectorizedSeqScan";
    plan.vectorization_mode = "Vector Batch: 1024 rows | Zero-Copy Streaming";
    double scan_cost = (rows / 1024.0) * 0.15 + rows * 0.003;
    plan.startup_cost = 0.00;
    plan.total_cost = scan_cost;
    plan.estimated_rows = rows;

    plan.plan_tree.push_back("-> VectorizedSeqScan on " + tbl_name + " (cost=0.00.." + 
        std::to_string(static_cast<int>(plan.total_cost)) + " rows=" + std::to_string(plan.estimated_rows) + 
        " chunks=" + std::to_string((rows + 1023) / 1024) + ")");
    return plan;
}

// =============================================================================
// 5. VECTORIZED AGGREGATE EXECUTION ENGINE (SIMD / 1024 CHUNKS)
// =============================================================================

inline Result execute_vectorized_aggregates(std::string_view sql, SharedDatabaseState& db) {
    std::string s = to_lower(sql);
    
    // Extract table name
    size_t from_p = s.find("from ");
    std::string tbl_name = "default";
    if (from_p != std::string_view::npos) {
        size_t end_p = s.find_first_of(" \t\n;", from_p + 5);
        if (end_p == std::string_view::npos) end_p = s.size();
        tbl_name = trim(s.substr(from_p + 5, end_p - (from_p + 5)));
    }

    auto tbl = db.get_table(tbl_name);
    if (!tbl) return create_result(0, false);

    std::shared_lock<std::shared_mutex> lock(tbl->mutex);

    // Extract requested aggregates from SELECT list
    size_t sel_p = s.find("select ");
    std::string_view proj_part = sql.substr(sel_p + 7, (from_p != std::string_view::npos ? from_p - (sel_p + 7) : sql.size() - (sel_p + 7)));

    std::stringstream pss{std::string(proj_part)};
    std::string item;
    std::vector<std::string> agg_exprs;
    while (std::getline(pss, item, ',')) {
        agg_exprs.push_back(trim(item));
    }

    std::vector<ColumnMetadata> cols;
    std::vector<types::Value> out_row;

    std::size_t N = tbl->sample_rows.size();
    std::size_t total_rows = tbl->total_rows;

    for (const auto& expr : agg_exprs) {
        std::string lower_e = to_lower(expr);
        ColumnMetadata col_meta;
        col_meta.table = tbl_name;
        col_meta.nullable = false;

        // COUNT(*) or COUNT(col)
        if (lower_e.starts_with("count(")) {
            col_meta.name = expr;
            col_meta.type = 4; // BIGINT
            cols.push_back(col_meta);
            out_row.push_back(types::Value(static_cast<std::int64_t>(total_rows)));
            continue;
        }

        // Determine aggregate type and target column name
        std::string func, col_name;
        size_t open_p = expr.find('(');
        size_t close_p = expr.find(')', open_p);
        if (open_p != std::string::npos && close_p != std::string::npos) {
            func = to_lower(trim(expr.substr(0, open_p)));
            col_name = trim(expr.substr(open_p + 1, close_p - (open_p + 1)));
        }

        std::size_t col_idx = tbl->get_col_idx(col_name);
        if (col_idx == static_cast<std::size_t>(-1) && !tbl->columns.empty()) {
            col_idx = 1; // Default fallback to column 1
        }

        col_meta.name = expr;

        // SIMD 1024-chunk processing over sample_rows
        double acc_sum = 0.0;
        double min_val = 1e30;
        double max_val = -1e30;
        std::size_t valid_count = 0;

        constexpr std::size_t CHUNK = 1024;
        for (std::size_t i = 0; i < N; i += CHUNK) {
            std::size_t limit = std::min(N, i + CHUNK);
            #pragma omp simd reduction(+:acc_sum, valid_count)
            for (std::size_t r = i; r < limit; ++r) {
                if (col_idx < tbl->sample_rows[r].size()) {
                    const auto& v = tbl->sample_rows[r][col_idx];
                    if (!v.is_null()) {
                        double d = value_as_double(v);
                        acc_sum += d;
                        valid_count++;
                    }
                }
            }
            for (std::size_t r = i; r < limit; ++r) {
                if (col_idx < tbl->sample_rows[r].size()) {
                    const auto& v = tbl->sample_rows[r][col_idx];
                    if (!v.is_null()) {
                        double d = value_as_double(v);
                        if (d < min_val) min_val = d;
                        if (d > max_val) max_val = d;
                    }
                }
            }
        }

        // Scale sample results to total_rows if applicable
        double scale = (N > 0 && total_rows > N) ? (static_cast<double>(total_rows) / static_cast<double>(N)) : 1.0;

        if (func == "sum") {
            col_meta.type = 6; // Double
            out_row.push_back(types::Value(acc_sum * scale));
        } else if (func == "avg") {
            col_meta.type = 6;
            double avg_val = (valid_count > 0) ? (acc_sum / valid_count) : 0.0;
            out_row.push_back(types::Value(avg_val));
        } else if (func == "min") {
            col_meta.type = 6;
            out_row.push_back(types::Value(min_val < 1e29 ? min_val : 0.0));
        } else if (func == "max") {
            col_meta.type = 6;
            out_row.push_back(types::Value(max_val > -1e29 ? max_val : 0.0));
        } else {
            col_meta.type = 6;
            out_row.push_back(types::Value(acc_sum));
        }
        cols.push_back(col_meta);
    }

    std::vector<std::vector<types::Value>> rows;
    rows.push_back(std::move(out_row));
    return create_result(0, true, std::move(cols), std::move(rows), 1, true);
}

// =============================================================================
// 6. IN-MEMORY HASH JOIN ALGORITHM
// =============================================================================

inline Result execute_hash_join(std::string_view sql, SharedDatabaseState& db) {
    std::string s = to_lower(sql);

    // Identify join type and positions
    enum class JoinType { Inner, Left, Right, Full, Cross };
    JoinType join_type = JoinType::Inner;
    size_t join_p = std::string::npos;
    size_t join_kw_len = 0;

    if ((join_p = s.find(" left outer join ")) != std::string::npos) {
        join_type = JoinType::Left;
        join_kw_len = 17;
    } else if ((join_p = s.find(" left join ")) != std::string::npos) {
        join_type = JoinType::Left;
        join_kw_len = 11;
    } else if ((join_p = s.find(" right outer join ")) != std::string::npos) {
        join_type = JoinType::Right;
        join_kw_len = 18;
    } else if ((join_p = s.find(" right join ")) != std::string::npos) {
        join_type = JoinType::Right;
        join_kw_len = 12;
    } else if ((join_p = s.find(" full outer join ")) != std::string::npos) {
        join_type = JoinType::Full;
        join_kw_len = 17;
    } else if ((join_p = s.find(" full join ")) != std::string::npos) {
        join_type = JoinType::Full;
        join_kw_len = 11;
    } else if ((join_p = s.find(" cross join ")) != std::string::npos) {
        join_type = JoinType::Cross;
        join_kw_len = 12;
    } else if ((join_p = s.find(" inner join ")) != std::string::npos) {
        join_type = JoinType::Inner;
        join_kw_len = 12;
    } else if ((join_p = s.find(" join ")) != std::string::npos) {
        join_type = JoinType::Inner;
        join_kw_len = 6;
    }

    if (join_p == std::string::npos) {
        return create_result(0, false);
    }

    size_t from_p = s.find("from ");
    if (from_p == std::string::npos || from_p >= join_p) {
        return create_result(0, false);
    }

    std::string t1_name = trim(s.substr(from_p + 5, join_p - (from_p + 5)));
    if (t1_name.ends_with(" inner")) t1_name = trim(t1_name.substr(0, t1_name.size() - 6));

    size_t on_p = s.find(" on ", join_p);
    size_t using_p = s.find(" using ", join_p);
    size_t after_t2 = s.size();
    if (on_p != std::string::npos) after_t2 = std::min(after_t2, on_p);
    if (using_p != std::string::npos) after_t2 = std::min(after_t2, using_p);
    size_t where_p = s.find(" where ", join_p);
    if (where_p != std::string::npos) after_t2 = std::min(after_t2, where_p);
    size_t limit_p = s.find(" limit ", join_p);
    if (limit_p != std::string::npos) after_t2 = std::min(after_t2, limit_p);

    std::string t2_name = trim(s.substr(join_p + join_kw_len, after_t2 - (join_p + join_kw_len)));
    while (!t2_name.empty() && (t2_name.back() == ';' || std::isspace(static_cast<unsigned char>(t2_name.back())))) {
        t2_name.pop_back();
    }

    std::string left_key = "id";
    std::string right_key = "id";

    if (using_p != std::string::npos && (on_p == std::string::npos || using_p < on_p)) {
        size_t u_start = s.find('(', using_p);
        size_t u_end = s.find(')', using_p);
        if (u_start != std::string::npos && u_end != std::string::npos && u_end > u_start) {
            std::string u_col = trim(s.substr(u_start + 1, u_end - u_start - 1));
            left_key = u_col;
            right_key = u_col;
        }
    } else if (on_p != std::string::npos) {
        size_t on_end = std::min(where_p != std::string::npos ? where_p : s.size(),
                                 limit_p != std::string::npos ? limit_p : s.size());
        std::string on_cond = trim(s.substr(on_p + 4, on_end - (on_p + 4)));
        size_t eq_p = on_cond.find('=');
        if (eq_p != std::string::npos) {
            left_key = trim(on_cond.substr(0, eq_p));
            right_key = trim(on_cond.substr(eq_p + 1));
        }
    }

    if (left_key.find('.') != std::string::npos) left_key = left_key.substr(left_key.find('.') + 1);
    if (right_key.find('.') != std::string::npos) right_key = right_key.substr(right_key.find('.') + 1);

    auto tbl1 = db.get_table(t1_name);
    auto tbl2 = db.get_table(t2_name);
    if (!tbl1 || !tbl2) return create_result(0, false);

    std::shared_lock<std::shared_mutex> lock1(tbl1->mutex);
    std::shared_lock<std::shared_mutex> lock2(tbl2->mutex);

    std::size_t k1_idx = tbl1->get_col_idx(left_key);
    std::size_t k2_idx = tbl2->get_col_idx(right_key);
    if (k1_idx == static_cast<std::size_t>(-1)) k1_idx = 0;
    if (k2_idx == static_cast<std::size_t>(-1)) k2_idx = 0;

    // Prepare Column Projection Plan
    struct JoinProjCol {
        bool from_tbl1{true};
        std::size_t col_idx{0};
        ColumnMetadata meta;
    };
    std::vector<JoinProjCol> proj_plan;

    size_t sel_p = s.find("select ");
    if (sel_p != std::string::npos && from_p > sel_p + 7) {
        std::string raw_cols = trim(std::string(sql.substr(sel_p + 7, from_p - (sel_p + 7))));
        if (!raw_cols.empty() && raw_cols != "*") {
            std::stringstream ss(raw_cols);
            std::string item;
            while (std::getline(ss, item, ',')) {
                std::string col_token = to_lower(trim(item));
                if (col_token.empty()) continue;

                bool from_t1 = true;
                std::string col_name = col_token;
                if (col_token.starts_with(t1_name + ".")) {
                    from_t1 = true;
                    col_name = col_token.substr(t1_name.size() + 1);
                } else if (col_token.starts_with(t2_name + ".")) {
                    from_t1 = false;
                    col_name = col_token.substr(t2_name.size() + 1);
                } else {
                    if (tbl1->get_col_idx(col_name) != static_cast<std::size_t>(-1)) {
                        from_t1 = true;
                    } else {
                        from_t1 = false;
                    }
                }

                auto target_tbl = from_t1 ? tbl1 : tbl2;
                std::size_t c_idx = target_tbl->get_col_idx(col_name);
                if (c_idx != static_cast<std::size_t>(-1) && c_idx < target_tbl->columns.size()) {
                    JoinProjCol pcol;
                    pcol.from_tbl1 = from_t1;
                    pcol.col_idx = c_idx;
                    pcol.meta = target_tbl->columns[c_idx];
                    pcol.meta.name = (from_t1 ? t1_name : t2_name) + "." + target_tbl->columns[c_idx].name;
                    proj_plan.push_back(pcol);
                }
            }
        }
    }

    if (proj_plan.empty()) {
        for (std::size_t i = 0; i < tbl1->columns.size(); ++i) {
            JoinProjCol pcol;
            pcol.from_tbl1 = true;
            pcol.col_idx = i;
            pcol.meta = tbl1->columns[i];
            pcol.meta.name = t1_name + "." + tbl1->columns[i].name;
            proj_plan.push_back(pcol);
        }
        for (std::size_t i = 0; i < tbl2->columns.size(); ++i) {
            JoinProjCol pcol;
            pcol.from_tbl1 = false;
            pcol.col_idx = i;
            pcol.meta = tbl2->columns[i];
            pcol.meta.name = t2_name + "." + tbl2->columns[i].name;
            proj_plan.push_back(pcol);
        }
    }

    auto build_joined_row = [&](const std::vector<types::Value>& r1, const std::vector<types::Value>& r2) {
        std::vector<types::Value> row;
        row.reserve(proj_plan.size());
        for (const auto& p : proj_plan) {
            const auto& src = p.from_tbl1 ? r1 : r2;
            if (p.col_idx < src.size()) {
                row.push_back(src[p.col_idx]);
            } else {
                row.push_back(types::Value());
            }
        }
        return row;
    };

    std::size_t max_results = 1000;
    if (limit_p != std::string::npos) {
        try { max_results = std::stoull(trim(s.substr(limit_p + 7))); } catch (...) {}
    }

    std::vector<std::vector<types::Value>> joined_rows;
    joined_rows.reserve(std::min<std::size_t>(max_results, tbl1->sample_rows.size()));

    if (join_type == JoinType::Cross) {
        for (const auto& r1 : tbl1->sample_rows) {
            if (joined_rows.size() >= max_results) break;
            for (const auto& r2 : tbl2->sample_rows) {
                if (joined_rows.size() >= max_results) break;
                joined_rows.push_back(build_joined_row(r1, r2));
            }
        }
    } else {
        // Hash table on tbl2: key -> row index in tbl2->sample_rows
        std::unordered_multimap<std::string, std::size_t> hash_table;
        hash_table.reserve(tbl2->sample_rows.size());
        for (std::size_t i = 0; i < tbl2->sample_rows.size(); ++i) {
            const auto& row = tbl2->sample_rows[i];
            if (k2_idx < row.size()) {
                hash_table.emplace(row[k2_idx].to_string(), i);
            }
        }

        std::vector<bool> t2_matched(tbl2->sample_rows.size(), false);
        const std::vector<types::Value> empty_t2(tbl2->columns.size(), types::Value());

        for (const auto& r1 : tbl1->sample_rows) {
            if (joined_rows.size() >= max_results) break;
            if (k1_idx >= r1.size()) continue;

            std::string probe_key = r1[k1_idx].to_string();
            auto range = hash_table.equal_range(probe_key);
            bool matched = false;
            for (auto it = range.first; it != range.second; ++it) {
                std::size_t r2_idx = it->second;
                t2_matched[r2_idx] = true;
                matched = true;
                joined_rows.push_back(build_joined_row(r1, tbl2->sample_rows[r2_idx]));
                if (joined_rows.size() >= max_results) break;
            }
            if (!matched && (join_type == JoinType::Left || join_type == JoinType::Full)) {
                joined_rows.push_back(build_joined_row(r1, empty_t2));
            }
        }

        if (join_type == JoinType::Right || join_type == JoinType::Full) {
            const std::vector<types::Value> empty_t1(tbl1->columns.size(), types::Value());
            for (std::size_t i = 0; i < tbl2->sample_rows.size(); ++i) {
                if (joined_rows.size() >= max_results) break;
                if (!t2_matched[i]) {
                    joined_rows.push_back(build_joined_row(empty_t1, tbl2->sample_rows[i]));
                }
            }
        }
    }

    std::vector<ColumnMetadata> cols;
    cols.reserve(proj_plan.size());
    for (const auto& p : proj_plan) {
        cols.push_back(p.meta);
    }

    std::size_t total_count = joined_rows.size();
    return create_result(0, true, std::move(cols), std::move(joined_rows), total_count, true);
}

// =============================================================================
// 7. INBUILT DATA EXTRACTION FUNCTIONS (JSON, STRING, MATH, COALESCE)
// =============================================================================

inline types::Value evaluate_scalar_func(
    const std::string& func_expr,
    const std::vector<types::Value>& row,
    const TableData& tbl,
    SharedDatabaseState* db_state = nullptr
) {
    std::string trimmed = trim(func_expr);
    if (trimmed.empty()) return types::Value();

    // Strip alias: expr AS alias
    size_t as_pos = ifind(trimmed, " as ");
    if (as_pos != std::string::npos && trimmed.front() != '\'' && trimmed.front() != '"') {
        trimmed = trim(trimmed.substr(0, as_pos));
    }

    std::string lower_t = to_lower(trimmed);

    // CASE ... WHEN ... THEN ... ELSE ... END
    if (lower_t.starts_with("case ") && lower_t.ends_with(" end")) {
        std::string_view case_body = std::string_view(trimmed).substr(5, trimmed.size() - 9);
        std::string s_body = to_lower(case_body);
        for (char& c : s_body) {
            if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        }
        size_t first_when = ifind(s_body, "when ");
        if (first_when == std::string::npos) return types::Value();

        std::string case_operand = trim(case_body.substr(0, first_when));
        bool is_simple_case = !case_operand.empty();
        types::Value operand_val;
        if (is_simple_case) {
            operand_val = evaluate_scalar_func(case_operand, row, tbl, db_state);
        }

        size_t w_pos = first_when;
        while (w_pos != std::string::npos && w_pos < case_body.size()) {
            size_t t_pos = ifind(s_body, " then ", w_pos + 5);
            if (t_pos == std::string::npos) break;
            std::string cond_or_val = trim(case_body.substr(w_pos + 5, t_pos - (w_pos + 5)));

            size_t next_w = ifind(s_body, " when ", t_pos + 6);
            size_t else_pos = ifind(s_body, " else ", t_pos + 6);
            size_t res_end = case_body.size();
            if (next_w != std::string::npos) res_end = std::min(res_end, next_w);
            if (else_pos != std::string::npos) res_end = std::min(res_end, else_pos);

            std::string res_str = trim(case_body.substr(t_pos + 6, res_end - (t_pos + 6)));

            bool matched = false;
            if (is_simple_case) {
                types::Value branch_val = parse_value_literal(cond_or_val);
                matched = (operand_val.to_string() == branch_val.to_string());
            } else {
                WherePredicate wp;
                std::string fake_sql = "where " + cond_or_val;
                if (parse_simple_where(fake_sql, wp)) {
                    matched = eval_predicate(wp, row, tbl, db_state);
                }
            }

            if (matched) {
                return evaluate_scalar_func(res_str, row, tbl, db_state);
            }
            w_pos = next_w != std::string::npos ? next_w + 1 : (else_pos != std::string::npos ? else_pos : case_body.size());
        }
        size_t el_p = ifind(s_body, " else ");
        if (el_p != std::string::npos) {
            std::string else_val = trim(case_body.substr(el_p + 6));
            return evaluate_scalar_func(else_val, row, tbl, db_state);
        }
        return types::Value();
    }

    // IS NOT NULL / IS NULL
    if (ifind(trimmed, " is not null") != std::string::npos) {
        size_t p = ifind(trimmed, " is not null");
        std::string c = trim(trimmed.substr(0, p));
        auto v = evaluate_scalar_func(c, row, tbl, db_state);
        return types::Value(!v.is_null());
    }
    if (ifind(trimmed, " is null") != std::string::npos) {
        size_t p = ifind(trimmed, " is null");
        std::string c = trim(trimmed.substr(0, p));
        auto v = evaluate_scalar_func(c, row, tbl, db_state);
        return types::Value(v.is_null());
    }

    // Check if it's a function call: func_name(...)
    size_t open_p = trimmed.find('(');
    size_t close_p = trimmed.rfind(')');
    if (open_p != std::string::npos && close_p != std::string::npos && close_p > open_p) {
        std::string func_name = trim(trimmed.substr(0, open_p));
        std::string_view args_str = std::string_view(trimmed).substr(open_p + 1, close_p - (open_p + 1));
        
        // Handle special CAST syntax: CAST(col AS type)
        if (to_lower(func_name) == "cast" || to_lower(func_name) == "try_cast") {
            size_t cast_as = ifind(args_str, " as ");
            if (cast_as != std::string::npos) {
                std::string val_expr = trim(args_str.substr(0, cast_as));
                std::string target_type = trim(args_str.substr(cast_as + 4));
                types::Value inner_val = evaluate_scalar_func(val_expr, row, tbl, db_state);
                return functions::FunctionRegistry::evaluate(func_name, {inner_val, types::Value(target_type, types::TypeID::Text)}, db_state);
            }
        }

        // Handle special EXTRACT syntax: EXTRACT(field FROM val)
        if (to_lower(func_name) == "extract") {
            size_t from_pos = ifind(args_str, " from ");
            if (from_pos != std::string_view::npos) {
                std::string field = trim(args_str.substr(0, from_pos));
                std::string temporal_col = trim(args_str.substr(from_pos + 6));
                types::Value col_val = evaluate_scalar_func(temporal_col, row, tbl, db_state);
                return functions::FunctionRegistry::evaluate("extract", {types::Value(field, types::TypeID::Text), col_val}, db_state);
            }
        }

        std::vector<std::string> raw_args = split_expressions(args_str);
        std::vector<types::Value> evaluated_args;
        evaluated_args.reserve(raw_args.size());

        for (const auto& a : raw_args) {
            evaluated_args.push_back(evaluate_scalar_func(a, row, tbl, db_state));
        }

        return functions::FunctionRegistry::evaluate(func_name, evaluated_args, db_state);
    }

    // Literals: Quoted string
    if (trimmed.size() >= 2 && ((trimmed.front() == '\'' && trimmed.back() == '\'') || (trimmed.front() == '"' && trimmed.back() == '"'))) {
        return types::Value(trimmed.substr(1, trimmed.size() - 2), types::TypeID::Text);
    }

    // Literals: Boolean
    if (to_lower(trimmed) == "true") return types::Value(true);
    if (to_lower(trimmed) == "false") return types::Value(false);
    if (to_lower(trimmed) == "null") return types::Value();

    // Literals: Number
    bool is_num = true;
    bool has_dot = false;
    size_t start_idx = (trimmed.front() == '+' || trimmed.front() == '-') ? 1 : 0;
    if (start_idx >= trimmed.size()) is_num = false;
    for (size_t i = start_idx; i < trimmed.size(); ++i) {
        if (trimmed[i] == '.') {
            if (has_dot) { is_num = false; break; }
            has_dot = true;
        } else if (!std::isdigit(static_cast<unsigned char>(trimmed[i]))) {
            is_num = false;
            break;
        }
    }
    if (is_num) {
        try {
            if (has_dot) return types::Value(std::stod(trimmed));
            return types::Value(static_cast<std::int64_t>(std::stoll(trimmed)));
        } catch (...) {}
    }

    // Column reference
    std::size_t col_idx = tbl.get_col_idx(trimmed);
    if (col_idx < row.size()) {
        return row[col_idx];
    }

    return types::Value(trimmed, types::TypeID::Text);
}

inline Result execute_extraction_query(std::string_view sql, SharedDatabaseState& db) {
    std::string s = to_lower(sql);
    
    // Extract table
    size_t from_p = s.find("from ");
    std::string tbl_name = "default";
    if (from_p != std::string_view::npos) {
        size_t end_p = s.find_first_of(" \t\n;", from_p + 5);
        if (end_p == std::string_view::npos) end_p = s.size();
        tbl_name = trim(s.substr(from_p + 5, end_p - (from_p + 5)));
    }

    // Extract projection expressions
    size_t sel_p = s.find("select ");
    bool is_distinct = false;
    size_t sel_start = sel_p + 7;
    if (s.substr(sel_p, 16).starts_with("select distinct ")) {
        is_distinct = true;
        sel_start = sel_p + 16;
    }
    std::string_view proj_part = sql.substr(sel_start, (from_p != std::string_view::npos ? from_p - sel_start : sql.size() - sel_start));
    std::vector<std::string> proj_exprs = split_expressions(proj_part);

    // If query has no FROM clause (e.g. SELECT 1, SELECT UPPER('foo'), SELECT NEXTVAL('seq'))
    if (from_p == std::string_view::npos) {
        std::vector<ColumnMetadata> cols;
        std::vector<types::Value> out_row;
        for (const auto& e : proj_exprs) {
            ColumnMetadata m;
            m.name = e;
            m.table = "dual";
            m.nullable = true;
            m.type = 3;
            cols.push_back(m);
            out_row.push_back(evaluate_scalar_func(e, {}, TableData{}, &db));
        }
        return create_result(0, true, std::move(cols), {std::move(out_row)}, 1, true);
    }

    std::shared_ptr<TableData> tbl;
    {
        std::shared_lock<std::shared_mutex> v_lk(db.db_mutex);
        auto v_it = db.views.find(to_lower(tbl_name));
        if (v_it != db.views.end()) {
            std::string v_q = v_it->second;
            v_lk.unlock();
            auto res = execute_extraction_query(v_q, db);
            auto* r_impl = res.get_impl();
            if (r_impl) {
                tbl = std::make_shared<TableData>();
                tbl->name = tbl_name;
                tbl->columns = r_impl->columns;
                for (size_t c = 0; c < tbl->columns.size(); ++c) {
                    tbl->column_indices[to_lower(tbl->columns[c].name)] = c;
                }
                tbl->sample_rows = r_impl->rows;
                tbl->total_rows = r_impl->rows.size();
            }
        }
    }
    if (!tbl) {
        tbl = db.get_table(tbl_name);
    }
    if (!tbl) return create_result(0, false);

    std::shared_lock<std::shared_mutex> lock(tbl->mutex);

    // Limit & Offset & Fetch First
    size_t limit_p = ifind(s, "limit ");
    std::size_t max_rows = 1000000;
    if (limit_p != std::string_view::npos) {
        try { max_rows = std::stoull(trim(s.substr(limit_p + 6))); } catch (...) {}
    }
    size_t fetch_p = ifind(s, "fetch first ");
    if (fetch_p != std::string_view::npos) {
        size_t after_ff = fetch_p + 12;
        try { max_rows = std::stoull(trim(s.substr(after_ff))); } catch (...) {}
    }
    size_t offset_p = ifind(s, "offset ");
    std::size_t skip_rows = 0;
    if (offset_p != std::string_view::npos) {
        try { skip_rows = std::stoull(trim(s.substr(offset_p + 7))); } catch (...) {}
    }

    // Handle SELECT *
    if (proj_exprs.size() == 1 && trim(proj_exprs[0]) == "*") {
        proj_exprs.clear();
        for (const auto& c : tbl->columns) {
            proj_exprs.push_back(c.name);
        }
    }

    enum class CompiledFuncType {
        None,
        Upper,
        Lower,
        Round,
        JsonExtract,
        Generic
    };

    struct CompiledProj {
        enum class Kind { DirectCol, FunctionOrExpr };
        Kind kind{Kind::FunctionOrExpr};
        std::size_t col_idx{0};
        std::string expr;
        CompiledFuncType func_type{CompiledFuncType::None};
        std::size_t arg_col_idx{0};
        double round_decimals{0.0};
        std::string json_key{""};
    };

    std::vector<CompiledProj> compiled;
    compiled.reserve(proj_exprs.size());
    std::vector<ColumnMetadata> cols;
    cols.reserve(proj_exprs.size());

    for (const auto& e : proj_exprs) {
        CompiledProj cp;
        std::string t = trim(e);
        cp.expr = t;

        ColumnMetadata m;
        m.name = t;
        m.table = tbl_name;
        m.nullable = true;
        m.type = 3;

        if (t.find('(') == std::string::npos) {
            std::size_t c_idx = tbl->get_col_idx(t);
            if (c_idx != static_cast<std::size_t>(-1)) {
                cp.kind = CompiledProj::Kind::DirectCol;
                cp.col_idx = c_idx;
                if (c_idx < tbl->columns.size()) {
                    m.type = tbl->columns[c_idx].type;
                    m.nullable = tbl->columns[c_idx].nullable;
                }
            }
        } else {
            size_t o_p = t.find('(');
            size_t c_p = t.rfind(')');
            if (o_p != std::string::npos && c_p != std::string::npos && c_p > o_p) {
                std::string fname = to_lower(trim(t.substr(0, o_p)));
                std::string inside = trim(t.substr(o_p + 1, c_p - o_p - 1));
                if (fname == "upper") {
                    std::size_t c_idx = tbl->get_col_idx(inside);
                    if (c_idx != static_cast<std::size_t>(-1)) {
                        cp.func_type = CompiledFuncType::Upper;
                        cp.arg_col_idx = c_idx;
                    }
                } else if (fname == "lower") {
                    std::size_t c_idx = tbl->get_col_idx(inside);
                    if (c_idx != static_cast<std::size_t>(-1)) {
                        cp.func_type = CompiledFuncType::Lower;
                        cp.arg_col_idx = c_idx;
                    }
                } else if (fname == "round") {
                    size_t comma = inside.find(',');
                    std::string c_name = (comma != std::string::npos) ? trim(inside.substr(0, comma)) : inside;
                    std::string dec_str = (comma != std::string::npos) ? trim(inside.substr(comma + 1)) : "0";
                    std::size_t c_idx = tbl->get_col_idx(c_name);
                    if (c_idx != static_cast<std::size_t>(-1)) {
                        cp.func_type = CompiledFuncType::Round;
                        cp.arg_col_idx = c_idx;
                        try { cp.round_decimals = std::stod(dec_str); } catch (...) { cp.round_decimals = 0.0; }
                    }
                } else if (fname == "json_extract") {
                    size_t comma = inside.find(',');
                    if (comma != std::string::npos) {
                        std::string c_name = trim(inside.substr(0, comma));
                        std::string path_str = trim(inside.substr(comma + 1));
                        while (!path_str.empty() && (path_str.front() == '\'' || path_str.front() == '"')) path_str = path_str.substr(1);
                        while (!path_str.empty() && (path_str.back() == '\'' || path_str.back() == '"')) path_str.pop_back();
                        if (path_str.starts_with("$.")) path_str = path_str.substr(2);
                        std::size_t c_idx = tbl->get_col_idx(c_name);
                        if (c_idx != static_cast<std::size_t>(-1)) {
                            cp.func_type = CompiledFuncType::JsonExtract;
                            cp.arg_col_idx = c_idx;
                            cp.json_key = path_str;
                        }
                    }
                }
            }
        }
        compiled.push_back(cp);
        cols.push_back(m);
    }

    WherePredicate pred;
    bool has_where = parse_simple_where(sql, pred);
    std::size_t filter_col_idx = static_cast<std::size_t>(-1);
    if (has_where) {
        filter_col_idx = tbl->get_col_idx(pred.column);
    }

    auto eval_row_compiled = [&](const std::vector<types::Value>& r) -> std::vector<types::Value> {
        std::vector<types::Value> out;
        out.reserve(compiled.size());
        for (const auto& cp : compiled) {
            if (cp.kind == CompiledProj::Kind::DirectCol) {
                out.push_back(cp.col_idx < r.size() ? r[cp.col_idx] : types::Value());
            } else if (cp.func_type == CompiledFuncType::Upper) {
                if (cp.arg_col_idx < r.size()) {
                    std::string s = r[cp.arg_col_idx].to_string();
                    for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                    out.push_back(types::Value(std::move(s), types::TypeID::Text));
                } else {
                    out.push_back(types::Value());
                }
            } else if (cp.func_type == CompiledFuncType::Lower) {
                if (cp.arg_col_idx < r.size()) {
                    std::string s = r[cp.arg_col_idx].to_string();
                    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    out.push_back(types::Value(std::move(s), types::TypeID::Text));
                } else {
                    out.push_back(types::Value());
                }
            } else if (cp.func_type == CompiledFuncType::Round) {
                double d = (cp.arg_col_idx < r.size()) ? r[cp.arg_col_idx].get<double>() : 0.0;
                double factor = std::pow(10.0, cp.round_decimals);
                out.push_back(types::Value(std::round(d * factor) / factor));
            } else if (cp.func_type == CompiledFuncType::JsonExtract) {
                std::string json_str = (cp.arg_col_idx < r.size()) ? r[cp.arg_col_idx].to_string() : "{}";
                size_t kpos = json_str.find("\"" + cp.json_key + "\"");
                if (kpos != std::string::npos) {
                    size_t colon = json_str.find(':', kpos);
                    if (colon != std::string::npos) {
                        size_t comma = json_str.find_first_of(",}", colon);
                        std::string val_str = trim(json_str.substr(colon + 1, comma - colon - 1));
                        if (val_str.size() >= 2 && val_str.front() == '"' && val_str.back() == '"') {
                            val_str = val_str.substr(1, val_str.size() - 2);
                        }
                        out.push_back(types::Value(val_str, types::TypeID::Text));
                    } else {
                        out.push_back(types::Value());
                    }
                } else {
                    out.push_back(types::Value());
                }
            } else {
                out.push_back(evaluate_scalar_func(cp.expr, r, *tbl, &db));
            }
        }
        return out;
    };

    if (has_where && filter_col_idx == 0 && pred.op == "=" && skip_rows == 0) {
        try {
            std::int64_t target_id = std::stoll(pred.value);
            if (target_id >= 1 && target_id <= static_cast<std::int64_t>(tbl->sample_rows.size())) {
                std::size_t idx = static_cast<std::size_t>(target_id - 1);
                if (idx < tbl->sample_rows.size() && !tbl->sample_rows[idx].empty() && tbl->sample_rows[idx][0].to_string() == pred.value) {
                    return create_result(0, true, std::move(cols), {eval_row_compiled(tbl->sample_rows[idx])}, 1, true);
                }
            }
        } catch (...) {}
    }

    std::vector<std::vector<types::Value>> rows;
    rows.reserve(std::min<std::size_t>(max_rows, tbl->sample_rows.size()));

    std::size_t skipped_count = 0;
    std::unordered_set<std::string> seen_distinct;

    for (const auto& r : tbl->sample_rows) {
        if (has_where && !eval_predicate(pred, r, *tbl, &db)) {
            continue;
        }

        if (skipped_count < skip_rows) {
            skipped_count++;
            continue;
        }

        if (rows.size() >= max_rows) break;

        auto out_row = eval_row_compiled(r);
        if (is_distinct) {
            std::string row_key;
            for (const auto& v : out_row) row_key += v.to_string() + "\x1F";
            if (!seen_distinct.insert(row_key).second) {
                continue;
            }
        }

        rows.push_back(std::move(out_row));
    }

    return create_result(0, true, std::move(cols), std::move(rows), rows.size(), true);
}

// =============================================================================
// 8. WINDOW FUNCTIONS (ROW_NUMBER, RANK, DENSE_RANK, LAG, LEAD)
// =============================================================================

inline Result execute_window_query(std::string_view sql, SharedDatabaseState& db) {
    std::string s = to_lower(sql);
    
    // Extract table
    size_t from_p = s.find("from ");
    std::string tbl_name = "default";
    if (from_p != std::string_view::npos) {
        size_t end_p = s.find_first_of(" \t\n;", from_p + 5);
        if (end_p == std::string_view::npos) end_p = s.size();
        tbl_name = trim(s.substr(from_p + 5, end_p - (from_p + 5)));
    }

    auto tbl = db.get_table(tbl_name);
    std::shared_ptr<TableData> view_tbl;
    if (!tbl) {
        std::shared_lock<std::shared_mutex> v_lk(db.db_mutex);
        auto v_it = db.views.find(to_lower(tbl_name));
        if (v_it != db.views.end()) {
            std::string v_query = v_it->second;
            v_lk.unlock();
            auto res = execute_extraction_query(v_query, db);
            auto* r_impl = res.get_impl();
            if (r_impl) {
                view_tbl = std::make_shared<TableData>();
                view_tbl->name = tbl_name;
                view_tbl->columns = r_impl->columns;
                for (size_t c = 0; c < view_tbl->columns.size(); ++c) {
                    view_tbl->column_indices[to_lower(view_tbl->columns[c].name)] = c;
                }
                view_tbl->sample_rows = r_impl->rows;
                view_tbl->total_rows = r_impl->rows.size();
                tbl = view_tbl;
            }
        }
    }
    if (!tbl) return create_result(0, false);

    std::shared_lock<std::shared_mutex> lock(tbl->mutex);

    // Limit
    size_t limit_p = s.find("limit ");
    std::size_t max_rows = 100;
    if (limit_p != std::string_view::npos) {
        try { max_rows = std::stoull(trim(s.substr(limit_p + 6))); } catch (...) {}
    }

    // Extract sort column from "order by <col>"
    size_t order_p = s.find("order by ");
    std::string sort_col = "id";
    bool desc = (s.find("desc") != std::string::npos);
    if (order_p != std::string_view::npos) {
        size_t col_start = order_p + 9;
        size_t col_end = s.find_first_of(" \t\n)", col_start);
        if (col_end != std::string_view::npos) {
            sort_col = trim(s.substr(col_start, col_end - col_start));
        }
    }

    std::size_t sort_idx = tbl->get_col_idx(sort_col);
    if (sort_idx == static_cast<std::size_t>(-1)) sort_idx = 0;

    // Sort a copy of sample rows
    std::vector<std::vector<types::Value>> sorted_rows = tbl->sample_rows;
    std::sort(sorted_rows.begin(), sorted_rows.end(), [&](const std::vector<types::Value>& a, const std::vector<types::Value>& b) {
        if (sort_idx >= a.size() || sort_idx >= b.size()) return false;
        double da = value_as_double(a[sort_idx]);
        double db_val = value_as_double(b[sort_idx]);
        return desc ? (da > db_val) : (da < db_val);
    });

    std::size_t row_count = std::min<std::size_t>(max_rows, sorted_rows.size());
    std::vector<std::vector<types::Value>> out_rows;
    out_rows.reserve(row_count);

    bool has_rownum = (s.find("row_number()") != std::string::npos);
    bool has_rank = (s.find("rank()") != std::string::npos);

    for (std::size_t i = 0; i < row_count; ++i) {
        std::vector<types::Value> row = sorted_rows[i];
        if (has_rownum) {
            row.push_back(types::Value(static_cast<std::int64_t>(i + 1)));
        }
        if (has_rank) {
            row.push_back(types::Value(static_cast<std::int64_t>(i + 1)));
        }
        out_rows.push_back(std::move(row));
    }

    std::vector<ColumnMetadata> cols = tbl->columns;
    if (has_rownum) {
        ColumnMetadata m;
        m.name = "row_number";
        m.type = 4; // BIGINT
        m.nullable = false;
        m.table = tbl_name;
        cols.push_back(m);
    }
    if (has_rank) {
        ColumnMetadata m;
        m.name = "rank";
        m.type = 4;
        m.nullable = false;
        m.table = tbl_name;
        cols.push_back(m);
    }

    return create_result(0, true, std::move(cols), std::move(out_rows), 0, true);
}

// =============================================================================
// 9. COMMON TABLE EXPRESSIONS (CTE - WITH ... AS ... SELECT ...)
// =============================================================================

inline Result execute_cte(std::string_view sql, SharedDatabaseState& db) {
    std::string s = to_lower(sql);
    size_t with_p = s.find("with ");
    size_t as_p = s.find(" as ");
    size_t paren_open = s.find('(', as_p);
    size_t paren_close = s.find(')', paren_open);

    if (with_p == std::string_view::npos || as_p == std::string_view::npos || 
        paren_open == std::string_view::npos || paren_close == std::string_view::npos) {
        return create_result(0, false);
    }

    std::string cte_name = trim(s.substr(with_p + 5, as_p - (with_p + 5)));
    std::string inner_sql = trim(sql.substr(paren_open + 1, paren_close - (paren_open + 1)));
    std::string outer_sql = trim(sql.substr(paren_close + 1));

    // Execute inner query to populate temporary table
    Result inner_res = execute_extraction_query(inner_sql, db);
    auto* impl = inner_res.get_impl();

    if (impl) {
        auto cte_tbl = db.get_or_create_table(cte_name);
        {
            std::unique_lock<std::shared_mutex> lock(cte_tbl->mutex);
            cte_tbl->columns = impl->columns;
            cte_tbl->sample_rows = impl->rows;
            cte_tbl->total_rows = impl->rows.size();
            for (size_t i = 0; i < cte_tbl->columns.size(); ++i) {
                cte_tbl->column_indices[to_lower(cte_tbl->columns[i].name)] = i;
            }
        }
        // Execute outer query on cte_tbl
        return execute_extraction_query(outer_sql, db);
    }

    return create_result(0, false);
}

// =============================================================================
// 10. GROUP BY & HAVING ENGINE
// =============================================================================

inline Result execute_group_by_query(std::string_view sql, SharedDatabaseState& db) {
    std::string s = to_lower(sql);

    // 1. Extract table name
    size_t from_p = s.find("from ");
    if (from_p == std::string::npos) return create_result(0, false);
    size_t end_from = s.find_first_of(" \t\n;", from_p + 5);
    if (end_from == std::string::npos) end_from = s.size();
    std::string tbl_name = trim(s.substr(from_p + 5, end_from - (from_p + 5)));

    auto tbl = db.get_table(tbl_name);
    if (!tbl) return create_result(0, false);

    std::shared_lock<std::shared_mutex> lock(tbl->mutex);

    // 2. Extract projection expressions
    size_t sel_p = s.find("select ");
    bool is_distinct = false;
    size_t sel_start = sel_p + 7;
    if (s.substr(sel_p, 16).starts_with("select distinct ")) {
        is_distinct = true;
        sel_start = sel_p + 16;
    }
    std::string_view proj_part = sql.substr(sel_start, from_p - sel_start);
    std::vector<std::string> proj_exprs = split_expressions(proj_part);

    // 3. Extract WHERE clause
    WherePredicate where_pred;
    bool has_where = parse_simple_where(sql, where_pred);

    // 4. Extract GROUP BY clause
    size_t gb_pos = s.find("group by ");
    if (gb_pos == std::string::npos) return create_result(0, false);
    size_t gb_start = gb_pos + 9;
    size_t gb_end = s.size();
    size_t having_p = s.find(" having ", gb_start);
    size_t order_p = s.find(" order by ", gb_start);
    size_t limit_p = s.find(" limit ", gb_start);

    if (having_p != std::string::npos) gb_end = std::min(gb_end, having_p);
    if (order_p != std::string::npos) gb_end = std::min(gb_end, order_p);
    if (limit_p != std::string::npos) gb_end = std::min(gb_end, limit_p);

    std::string_view gb_clause = sql.substr(gb_start, gb_end - gb_start);
    while (!gb_clause.empty() && (gb_clause.back() == ';' || std::isspace(static_cast<unsigned char>(gb_clause.back())))) {
        gb_clause.remove_suffix(1);
    }
    std::vector<std::string> gb_cols_raw = split_expressions(gb_clause);
    std::vector<std::string> group_cols;
    std::vector<std::size_t> group_col_indices;
    for (const auto& c : gb_cols_raw) {
        std::string c_clean = trim(c);
        while (!c_clean.empty() && (c_clean.back() == ';' || std::isspace(static_cast<unsigned char>(c_clean.back())))) {
            c_clean.pop_back();
        }
        c_clean = trim(c_clean);
        group_cols.push_back(c_clean);
        std::size_t idx = tbl->get_col_idx(c_clean);
        group_col_indices.push_back(idx);
    }

    // 5. Extract HAVING condition if present
    std::string having_clause;
    if (having_p != std::string::npos) {
        size_t h_start = having_p + 8;
        size_t h_end = s.size();
        if (order_p != std::string::npos && order_p > h_start) h_end = std::min(h_end, order_p);
        if (limit_p != std::string::npos && limit_p > h_start) h_end = std::min(h_end, limit_p);
        having_clause = trim(sql.substr(h_start, h_end - h_start));
        while (!having_clause.empty() && (having_clause.back() == ';' || std::isspace(static_cast<unsigned char>(having_clause.back())))) {
            having_clause.pop_back();
        }
        having_clause = trim(having_clause);
    }

    // 6. Analyze projections for aggregates
    struct ProjItem {
        std::string raw_expr;
        std::string alias;
        bool is_group_key{false};
        std::size_t group_key_idx{0};
        bool is_agg{false};
        std::string agg_func;
        std::string agg_arg;
        std::size_t agg_col_idx{static_cast<std::size_t>(-1)};
        ColumnMetadata meta;
    };

    std::vector<ProjItem> proj_items;
    std::vector<ColumnMetadata> out_cols;

    for (const auto& expr : proj_exprs) {
        ProjItem item;
        std::string t = trim(expr);
        item.raw_expr = t;
        size_t as_p = ifind(t, " as ");
        if (as_p != std::string::npos) {
            item.alias = trim(t.substr(as_p + 4));
            t = trim(t.substr(0, as_p));
        } else {
            item.alias = t;
        }

        std::string lower_t = to_lower(t);

        bool matched_gk = false;
        for (std::size_t g = 0; g < group_cols.size(); ++g) {
            if (to_lower(group_cols[g]) == lower_t) {
                item.is_group_key = true;
                item.group_key_idx = g;
                matched_gk = true;
                break;
            }
        }

        if (!matched_gk) {
            std::vector<std::string> aggs = {"count(", "sum(", "avg(", "min(", "max("};
            for (const auto& agg_prefix : aggs) {
                if (lower_t.starts_with(agg_prefix)) {
                    item.is_agg = true;
                    item.agg_func = agg_prefix.substr(0, agg_prefix.size() - 1);
                    size_t c_end = t.rfind(')');
                    if (c_end != std::string::npos) {
                        item.agg_arg = trim(t.substr(agg_prefix.size(), c_end - agg_prefix.size()));
                        if (item.agg_arg != "*") {
                            item.agg_col_idx = tbl->get_col_idx(item.agg_arg);
                        }
                    }
                    break;
                }
            }
        }

        ColumnMetadata m;
        m.name = item.alias;
        m.table = tbl_name;
        m.nullable = true;
        m.type = item.is_agg ? (item.agg_func == "count" ? 4 : 6) : 9;
        item.meta = m;
        out_cols.push_back(m);
        proj_items.push_back(item);
    }

    // 7. Group Accumulators
    struct GroupState {
        std::vector<types::Value> key_vals;
        std::size_t count{0};
        std::vector<double> sums;
        std::vector<double> mins;
        std::vector<double> maxs;
        std::vector<std::size_t> non_null_counts;
    };

    std::unordered_map<std::string, GroupState> groups;
    std::vector<std::string> group_order;

    for (const auto& row : tbl->sample_rows) {
        if (has_where && !eval_predicate(where_pred, row, *tbl, &db)) {
            continue;
        }

        std::string gkey;
        std::vector<types::Value> key_vals;
        key_vals.reserve(group_col_indices.size());
        for (std::size_t c_idx : group_col_indices) {
            if (c_idx < row.size()) {
                key_vals.push_back(row[c_idx]);
                gkey += row[c_idx].to_string() + "\x1F";
            } else {
                key_vals.push_back(types::Value());
                gkey += "NULL\x1F";
            }
        }

        auto it = groups.find(gkey);
        if (it == groups.end()) {
            GroupState gs;
            gs.key_vals = key_vals;
            gs.count = 0;
            gs.sums.resize(proj_items.size(), 0.0);
            gs.mins.resize(proj_items.size(), 1e30);
            gs.maxs.resize(proj_items.size(), -1e30);
            gs.non_null_counts.resize(proj_items.size(), 0);
            group_order.push_back(gkey);
            it = groups.emplace(gkey, std::move(gs)).first;
        }

        GroupState& state = it->second;
        state.count++;

        for (std::size_t p = 0; p < proj_items.size(); ++p) {
            const auto& pi = proj_items[p];
            if (pi.is_agg) {
                if (pi.agg_arg == "*") {
                    state.non_null_counts[p]++;
                } else if (pi.agg_col_idx < row.size()) {
                    const auto& v = row[pi.agg_col_idx];
                    if (!v.is_null()) {
                        double d = value_as_double(v);
                        state.sums[p] += d;
                        if (d < state.mins[p]) state.mins[p] = d;
                        if (d > state.maxs[p]) state.maxs[p] = d;
                        state.non_null_counts[p]++;
                    }
                }
            }
        }
    }

    // 8. Build output rows & evaluate HAVING
    std::vector<std::vector<types::Value>> out_rows;
    out_rows.reserve(group_order.size());

    for (const auto& gkey : group_order) {
        const auto& state = groups[gkey];

        if (!having_clause.empty()) {
            std::string h_lower = to_lower(having_clause);
            bool pass = true;
            bool matched_agg = false;
            for (std::size_t p = 0; p < proj_items.size(); ++p) {
                const auto& pi = proj_items[p];
                if (pi.is_agg && h_lower.find(to_lower(pi.raw_expr)) != std::string::npos) {
                    matched_agg = true;
                    double val = (pi.agg_func == "count") ? static_cast<double>(state.count) :
                                 (pi.agg_func == "avg") ? (state.non_null_counts[p] > 0 ? state.sums[p] / state.non_null_counts[p] : 0.0) :
                                 (pi.agg_func == "sum") ? state.sums[p] :
                                 (pi.agg_func == "min") ? state.mins[p] : state.maxs[p];

                    std::vector<std::string> ops = {">=", "<=", "!=", ">", "<", "="};
                    for (const auto& op : ops) {
                        size_t op_pos = h_lower.find(op);
                        if (op_pos != std::string::npos) {
                            try {
                                double target = std::stod(trim(having_clause.substr(op_pos + op.size())));
                                if (op == ">" && !(val > target)) pass = false;
                                else if (op == "<" && !(val < target)) pass = false;
                                else if (op == ">=" && !(val >= target)) pass = false;
                                else if (op == "<=" && !(val <= target)) pass = false;
                                else if (op == "=" && !(std::abs(val - target) < 1e-6)) pass = false;
                                else if (op == "!=" && !(std::abs(val - target) >= 1e-6)) pass = false;
                            } catch (...) {}
                            break;
                        }
                    }
                }
            }
            if (!matched_agg && h_lower.find("count") != std::string::npos) {
                double val = static_cast<double>(state.count);
                std::vector<std::string> ops = {">=", "<=", "!=", ">", "<", "="};
                for (const auto& op : ops) {
                    size_t op_pos = h_lower.find(op);
                    if (op_pos != std::string::npos) {
                        try {
                            double target = std::stod(trim(having_clause.substr(op_pos + op.size())));
                            if (op == ">" && !(val > target)) pass = false;
                            else if (op == "<" && !(val < target)) pass = false;
                            else if (op == ">=" && !(val >= target)) pass = false;
                            else if (op == "<=" && !(val <= target)) pass = false;
                            else if (op == "=" && !(std::abs(val - target) < 1e-6)) pass = false;
                            else if (op == "!=" && !(std::abs(val - target) >= 1e-6)) pass = false;
                        } catch (...) {}
                        break;
                    }
                }
            }
            if (!pass) continue;
        }

        std::vector<types::Value> row;
        row.reserve(proj_items.size());
        for (std::size_t p = 0; p < proj_items.size(); ++p) {
            const auto& pi = proj_items[p];
            if (pi.is_group_key) {
                row.push_back(pi.group_key_idx < state.key_vals.size() ? state.key_vals[pi.group_key_idx] : types::Value());
            } else if (pi.is_agg) {
                if (pi.agg_func == "count") {
                    row.push_back(types::Value(static_cast<std::int64_t>(state.count)));
                } else if (pi.agg_func == "sum") {
                    row.push_back(types::Value(state.sums[p]));
                } else if (pi.agg_func == "avg") {
                    double avg_val = (state.non_null_counts[p] > 0) ? (state.sums[p] / state.non_null_counts[p]) : 0.0;
                    row.push_back(types::Value(avg_val));
                } else if (pi.agg_func == "min") {
                    row.push_back(types::Value(state.mins[p] < 1e29 ? state.mins[p] : 0.0));
                } else if (pi.agg_func == "max") {
                    row.push_back(types::Value(state.maxs[p] > -1e29 ? state.maxs[p] : 0.0));
                } else {
                    row.push_back(types::Value());
                }
            } else {
                row.push_back(types::Value(pi.raw_expr, types::TypeID::Text));
            }
        }
        out_rows.push_back(std::move(row));
    }

    // Limit & Offset & Fetch First
    limit_p = ifind(s, "limit ");
    size_t offset_p = ifind(s, "offset ");
    size_t fetch_p = ifind(s, "fetch first ");
    if (fetch_p == std::string::npos) fetch_p = ifind(s, "fetch next ");
    std::size_t max_rows = 1000000;
    std::size_t skip_rows = 0;
    if (limit_p != std::string::npos) {
        try { max_rows = std::stoull(trim(s.substr(limit_p + 6))); } catch (...) {}
    }
    if (fetch_p != std::string::npos) {
        size_t after_ff = fetch_p + 12;
        try { max_rows = std::stoull(trim(s.substr(after_ff))); } catch (...) {}
    }
    if (offset_p != std::string::npos) {
        try { skip_rows = std::stoull(trim(s.substr(offset_p + 7))); } catch (...) {}
    }

    if (skip_rows > 0 || out_rows.size() > max_rows) {
        std::vector<std::vector<types::Value>> sliced;
        std::size_t start_i = std::min(skip_rows, out_rows.size());
        std::size_t end_i = std::min(start_i + max_rows, out_rows.size());
        for (std::size_t i = start_i; i < end_i; ++i) {
            sliced.push_back(std::move(out_rows[i]));
        }
        out_rows = std::move(sliced);
    }

    return create_result(0, true, std::move(out_cols), std::move(out_rows), out_rows.size(), true);
}

// =============================================================================
// 11. ALTER TABLE ENGINE
// =============================================================================

inline Result execute_alter_table(std::string_view sql, SharedDatabaseState& db) {
    std::string s = to_lower(sql);
    size_t at_pos = s.find("alter table ");
    if (at_pos == std::string::npos) return create_result(0, false);
    size_t tbl_start = at_pos + 12;
    size_t tbl_end = s.find_first_of(" \t\n", tbl_start);
    if (tbl_end == std::string::npos) return create_result(0, false);
    std::string tbl_name = trim(s.substr(tbl_start, tbl_end - tbl_start));

    auto tbl = db.get_table(tbl_name);
    if (!tbl) return create_result(0, false);

    std::unique_lock<std::shared_mutex> lock(tbl->mutex);

    size_t add_pos = s.find(" add column ", tbl_end);
    if (add_pos == std::string::npos) add_pos = s.find(" add ", tbl_end);

    if (add_pos != std::string::npos) {
        size_t def_start = add_pos + (s.find(" add column ", tbl_end) != std::string::npos ? 12 : 5);
        std::string col_def = trim(sql.substr(def_start));
        std::string col_def_low = to_lower(col_def);
        bool if_not_exists = false;
        if (col_def_low.starts_with("if not exists ")) {
            if_not_exists = true;
            col_def = trim(col_def.substr(14));
            col_def_low = to_lower(col_def);
        }

        std::stringstream ss{col_def};
        std::string col_name, col_type;
        ss >> col_name >> col_type;
        if (!col_name.empty()) {
            if (tbl->get_col_idx(col_name) != static_cast<std::size_t>(-1)) {
                if (if_not_exists) return create_result(0, false);
                return create_result(0, false);
            }

            ColumnMetadata meta;
            meta.name = col_name;
            meta.table = tbl_name;
            meta.nullable = (ifind(col_def, "not null") == std::string::npos);
            std::string ltype = to_lower(col_type);
            if (ltype.starts_with("int") || ltype.starts_with("bigint") || ltype.starts_with("smallint") || ltype.starts_with("tinyint")) {
                meta.type = 1;
            } else if (ltype.starts_with("float") || ltype.starts_with("double") || ltype.starts_with("real") || ltype.starts_with("numeric") || ltype.starts_with("decimal")) {
                meta.type = 2;
            } else if (ltype.starts_with("bool")) {
                meta.type = 3;
            } else {
                meta.type = 9;
            }

            types::Value def_val;
            size_t def_pos = ifind(col_def, "default ");
            if (def_pos != std::string::npos) {
                std::string d_expr = trim(col_def.substr(def_pos + 8));
                size_t semi = d_expr.find(';');
                if (semi != std::string::npos) d_expr = trim(d_expr.substr(0, semi));
                def_val = parse_value_literal(d_expr);
            }
            tbl->column_indices[to_lower(col_name)] = tbl->columns.size();
            tbl->columns.push_back(meta);
            for (auto& row : tbl->sample_rows) {
                row.push_back(def_val);
            }
            return create_result(1, false);
        }
    }

    size_t drop_pos = s.find(" drop column ", tbl_end);
    if (drop_pos == std::string::npos) drop_pos = s.find(" drop ", tbl_end);
    if (drop_pos != std::string::npos) {
        size_t c_start = drop_pos + (s.find(" drop column ", tbl_end) != std::string::npos ? 13 : 6);
        std::string col_name = trim(sql.substr(c_start));
        std::string col_name_low = to_lower(col_name);
        bool if_exists = false;
        if (col_name_low.starts_with("if exists ")) {
            if_exists = true;
            col_name = trim(col_name.substr(10));
        }
        size_t semi = col_name.find(';');
        if (semi != std::string::npos) col_name = trim(col_name.substr(0, semi));
        std::size_t idx = tbl->get_col_idx(col_name);
        if (idx != static_cast<std::size_t>(-1)) {
            tbl->columns.erase(tbl->columns.begin() + idx);
            for (auto& row : tbl->sample_rows) {
                if (idx < row.size()) row.erase(row.begin() + idx);
            }
            tbl->column_indices.clear();
            for (size_t c = 0; c < tbl->columns.size(); ++c) {
                tbl->column_indices[to_lower(tbl->columns[c].name)] = c;
            }
            return create_result(1, false);
        } else if (if_exists) {
            return create_result(0, false);
        }
    }

    size_t rename_to = s.find(" rename to ", tbl_end);
    if (rename_to == std::string::npos) rename_to = s.find(" rename ", tbl_end);
    if (rename_to != std::string::npos && s.find(" rename column ", tbl_end) == std::string::npos) {
        size_t n_start = rename_to + (s.find(" rename to ", tbl_end) != std::string::npos ? 11 : 8);
        std::string new_name = trim(sql.substr(n_start));
        size_t semi = new_name.find(';');
        if (semi != std::string::npos) new_name = trim(new_name.substr(0, semi));
        if (!new_name.empty()) {
            std::unique_lock<std::shared_mutex> db_l(db.db_mutex);
            db.tables.erase(to_lower(tbl_name));
            db.tables.erase(tbl_name);
            tbl->name = new_name;
            for (auto& c : tbl->columns) c.table = new_name;
            db.tables[to_lower(new_name)] = tbl;
            return create_result(1, false);
        }
    }

    size_t rename_col = s.find(" rename column ", tbl_end);
    if (rename_col != std::string::npos) {
        size_t to_pos = s.find(" to ", rename_col + 15);
        if (to_pos != std::string::npos) {
            std::string old_col = trim(sql.substr(rename_col + 15, to_pos - (rename_col + 15)));
            std::string new_col = trim(sql.substr(to_pos + 4));
            size_t semi = new_col.find(';');
            if (semi != std::string::npos) new_col = trim(new_col.substr(0, semi));
            std::size_t idx = tbl->get_col_idx(old_col);
            if (idx != static_cast<std::size_t>(-1)) {
                tbl->columns[idx].name = new_col;
                tbl->column_indices.clear();
                for (size_t c = 0; c < tbl->columns.size(); ++c) {
                    tbl->column_indices[to_lower(tbl->columns[c].name)] = c;
                }
                return create_result(1, false);
            }
        }
    }

    return create_result(0, false);
}

// =============================================================================
// 12. SET OPERATIONS (UNION, UNION ALL, INTERSECT, EXCEPT)
// =============================================================================

inline Result execute_set_operation(std::string_view sql, SharedDatabaseState& db) {
    std::string s = to_lower(sql);
    enum class SetOp { UnionAll, Union, Intersect, Except };
    SetOp op = SetOp::Union;
    size_t op_pos = std::string::npos;
    size_t op_len = 0;

    if ((op_pos = s.find(" union all ")) != std::string::npos) {
        op = SetOp::UnionAll;
        op_len = 11;
    } else if ((op_pos = s.find(" union ")) != std::string::npos) {
        op = SetOp::Union;
        op_len = 7;
    } else if ((op_pos = s.find(" intersect ")) != std::string::npos) {
        op = SetOp::Intersect;
        op_len = 11;
    } else if ((op_pos = s.find(" except ")) != std::string::npos) {
        op = SetOp::Except;
        op_len = 8;
    }

    if (op_pos == std::string::npos) return create_result(0, false);

    std::string left_sql = trim(sql.substr(0, op_pos));
    std::string right_sql = trim(sql.substr(op_pos + op_len));

    Result left_res = execute_extraction_query(left_sql, db);
    Result right_res = execute_extraction_query(right_sql, db);

    auto* left_impl = left_res.get_impl();
    auto* right_impl = right_res.get_impl();
    if (!left_impl) return right_res;
    if (!right_impl) return left_res;

    std::vector<ColumnMetadata> cols = left_impl->columns;
    std::vector<std::vector<types::Value>> combined;

    if (op == SetOp::UnionAll) {
        combined = left_impl->rows;
        combined.insert(combined.end(), right_impl->rows.begin(), right_impl->rows.end());
    } else if (op == SetOp::Union) {
        std::unordered_set<std::string> seen;
        for (const auto& r : left_impl->rows) {
            std::string key;
            for (const auto& v : r) key += v.to_string() + "\x1F";
            if (seen.insert(key).second) combined.push_back(r);
        }
        for (const auto& r : right_impl->rows) {
            std::string key;
            for (const auto& v : r) key += v.to_string() + "\x1F";
            if (seen.insert(key).second) combined.push_back(r);
        }
    } else if (op == SetOp::Intersect) {
        std::unordered_set<std::string> right_set;
        for (const auto& r : right_impl->rows) {
            std::string key;
            for (const auto& v : r) key += v.to_string() + "\x1F";
            right_set.insert(key);
        }
        std::unordered_set<std::string> emitted;
        for (const auto& r : left_impl->rows) {
            std::string key;
            for (const auto& v : r) key += v.to_string() + "\x1F";
            if (right_set.count(key) && emitted.insert(key).second) {
                combined.push_back(r);
            }
        }
    } else if (op == SetOp::Except) {
        std::unordered_set<std::string> right_set;
        for (const auto& r : right_impl->rows) {
            std::string key;
            for (const auto& v : r) key += v.to_string() + "\x1F";
            right_set.insert(key);
        }
        std::unordered_set<std::string> emitted;
        for (const auto& r : left_impl->rows) {
            std::string key;
            for (const auto& v : r) key += v.to_string() + "\x1F";
            if (!right_set.count(key) && emitted.insert(key).second) {
                combined.push_back(r);
            }
        }
    }

    return create_result(0, true, std::move(cols), std::move(combined), combined.size(), true);
}

} // namespace nemo::api::query_engine

#endif // NEMO_SRC_API_QUERY_ENGINE_HPP
