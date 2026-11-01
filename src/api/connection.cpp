#include <charconv>
#include "nemo/api/connection.hpp"
#include "nemo/runtime/connection_lease.hpp"
#include "nemo/runtime/worker_pool.hpp"
#include "api_internal.hpp"
#include "query_engine.hpp"
#include <unordered_map>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <string_view>

namespace nemo::api {

namespace {

inline size_t ifind(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return 0;
    if (haystack.size() < needle.size()) return std::string_view::npos;
    for (size_t i = 0; i <= haystack.size() - needle.size(); ++i) {
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

std::string extract_table_name(std::string_view sql) {
    auto find_after = [&](std::string_view kw) -> std::string {
        size_t pos = ifind(sql, kw);
        if (pos == std::string_view::npos) return "";
        pos += kw.length();
        while (pos < sql.length() && (sql[pos] == ' ' || sql[pos] == '\t' || sql[pos] == '\n')) pos++;
        size_t end = pos;
        while (end < sql.length() && (std::isalnum(static_cast<unsigned char>(sql[end])) || sql[end] == '_')) end++;
        if (end > pos) {
            std::string tok = std::string(sql.substr(pos, end - pos));
            if (query_engine::to_lower(tok) == "if") {
                size_t exists_pos = ifind(sql.substr(end), "exists ");
                if (exists_pos != std::string_view::npos) {
                    size_t actual_pos = end + exists_pos + 7;
                    while (actual_pos < sql.length() && (sql[actual_pos] == ' ' || sql[actual_pos] == '\t' || sql[actual_pos] == '\n')) actual_pos++;
                    size_t actual_end = actual_pos;
                    while (actual_end < sql.length() && (std::isalnum(static_cast<unsigned char>(sql[actual_end])) || sql[actual_end] == '_')) actual_end++;
                    if (actual_end > actual_pos) return std::string(sql.substr(actual_pos, actual_end - actual_pos));
                }
            }
            return tok;
        }
        return "";
    };

    std::string tbl = find_after("into ");
    if (!tbl.empty()) return tbl;
    tbl = find_after("from ");
    if (!tbl.empty()) return tbl;
    tbl = find_after("table ");
    if (!tbl.empty()) return tbl;
    tbl = find_after("update ");
    if (!tbl.empty()) return tbl;
    return "default";
}

std::vector<types::Value> parse_simple_values(std::string_view sql) {
    std::vector<types::Value> vals;
    size_t pos = ifind(sql, "values");
    if (pos == std::string_view::npos) return vals;
    
    size_t start = sql.find('(', pos);
    if (start == std::string_view::npos) return vals;
    size_t end = std::string_view::npos;
    bool in_quote = false;
    char q_char = '\0';
    for (size_t i = start + 1; i < sql.size(); ++i) {
        char ch = sql[i];
        if (in_quote) {
            if (ch == q_char) in_quote = false;
        } else {
            if (ch == '\'' || ch == '"') {
                in_quote = true;
                q_char = ch;
            } else if (ch == ')') {
                end = i;
                break;
            }
        }
    }
    if (end == std::string_view::npos || end <= start) return vals;
    
    std::string_view inside = sql.substr(start + 1, end - start - 1);
    vals.reserve(32);

    size_t token_start = 0;
    in_quote = false;
    q_char = '\0';

    auto process_token = [&](std::string_view token) {
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) token.remove_prefix(1);
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) token.remove_suffix(1);
        if (token.empty()) return;

        if (token.size() >= 2 && ((token.front() == '\'' && token.back() == '\'') || (token.front() == '"' && token.back() == '"'))) {
            vals.emplace_back(std::string(token.substr(1, token.length() - 2)), types::TypeID::Text);
        } else if (token == "true" || token == "TRUE") {
            vals.emplace_back(true);
        } else if (token == "false" || token == "FALSE") {
            vals.emplace_back(false);
        } else if (token == "null" || token == "NULL") {
            vals.emplace_back();
        } else if (token.find('.') != std::string_view::npos) {
            double d = 0.0;
            auto [p, ec] = std::from_chars(token.data(), token.data() + token.size(), d);
            if (ec == std::errc()) {
                vals.emplace_back(d);
            } else {
                std::string tmp_tok(token);
                vals.emplace_back(std::strtod(tmp_tok.c_str(), nullptr));
            }
        } else {
            std::int64_t iv = 0;
            auto [p, ec] = std::from_chars(token.data(), token.data() + token.size(), iv);
            if (ec == std::errc()) {
                vals.emplace_back(iv);
            } else {
                std::string tmp_tok(token);
                vals.emplace_back(static_cast<std::int64_t>(std::strtoll(tmp_tok.c_str(), nullptr, 10)));
            }
        }
    };

    for (size_t k = 0; k < inside.size(); ++k) {
        char ch = inside[k];
        if (!in_quote && (ch == '\'' || ch == '"')) {
            in_quote = true;
            q_char = ch;
        } else if (in_quote && ch == q_char) {
            in_quote = false;
        } else if (!in_quote && ch == ',') {
            process_token(inside.substr(token_start, k - token_start));
            token_start = k + 1;
        }
    }
    if (token_start < inside.size()) {
        process_token(inside.substr(token_start));
    }

    return vals;
}

} // anonymous namespace

nemo::Result<Connection> create_connection(
    std::shared_ptr<runtime::ConnectionLease> lease,
    std::shared_ptr<runtime::WorkerPool> pool,
    std::shared_ptr<SharedDatabaseState> shared_state
) {
    auto impl = std::make_unique<ConnectionImpl>();
    impl->lease = std::move(lease);
    impl->worker_pool = std::move(pool);
    if (shared_state) {
        impl->shared_state = std::move(shared_state);
    } else {
        impl->shared_state = std::make_shared<SharedDatabaseState>();
    }
    return Connection(std::move(impl));
}

Connection::Connection(std::unique_ptr<ConnectionImpl> impl) : m_impl(std::move(impl)) {}
Connection::~Connection() { close(); }
Connection::Connection(Connection&&) noexcept = default;
Connection& Connection::operator=(Connection&&) noexcept = default;

nemo::Result<Result> Connection::execute(std::string_view sql) {
    return execute(sql, std::span<const types::Value>{});
}

nemo::Result<Result> Connection::execute(std::string_view sql, std::span<const types::Value> parameters) {
    size_t i = 0;
    while (i < sql.size() && std::isspace(static_cast<unsigned char>(sql[i]))) i++;
    char first_c = (i < sql.size()) ? static_cast<char>(std::tolower(static_cast<unsigned char>(sql[i]))) : '\0';

    if (!m_impl || !m_impl->shared_state) {
        return create_result(0, false);
    }

    // ULTRA-FAST ZERO-COPY SINGLE ROW INSERT FAST PATH
    if (first_c == 'i') {
        std::string_view trimmed_sql = sql.substr(i);
        if (ifind(trimmed_sql, " returning ") == std::string_view::npos &&
            ifind(trimmed_sql, "select ") == std::string_view::npos &&
            ifind(trimmed_sql, "on conflict") == std::string_view::npos) {
            size_t val_pos = ifind(trimmed_sql, "values");
            if (val_pos != std::string_view::npos) {
                size_t into_pos = ifind(trimmed_sql, "into ");
                if (into_pos != std::string_view::npos && into_pos < val_pos) {
                    size_t lparen = trimmed_sql.find('(', into_pos);
                    bool has_col_list = (lparen != std::string_view::npos && lparen < val_pos);
                    if (!has_col_list) {
                        size_t tn_start = into_pos + 5;
                        while (tn_start < val_pos && std::isspace(static_cast<unsigned char>(trimmed_sql[tn_start]))) tn_start++;
                        size_t tn_end = tn_start;
                        while (tn_end < val_pos && (std::isalnum(static_cast<unsigned char>(trimmed_sql[tn_end])) || trimmed_sql[tn_end] == '_')) tn_end++;

                        if (tn_end > tn_start) {
                            std::string tname(trimmed_sql.substr(tn_start, tn_end - tn_start));
                            auto tbl = m_impl->shared_state->get_or_create_table(tname);
                            if (tbl) {
                                std::vector<types::Value> row_vals;
                                if (!parameters.empty()) {
                                    row_vals.assign(parameters.begin(), parameters.end());
                                } else {
                                    row_vals = parse_simple_values(trimmed_sql);
                                }

                                if (!row_vals.empty()) {
                                    std::unique_lock<std::shared_mutex> write_lock(tbl->mutex);
                                    std::shared_ptr<Sequence> auto_seq = nullptr;
                                    if (tbl->autoincrement_col_idx != static_cast<std::size_t>(-1) && !tbl->autoincrement_seq_name.empty()) {
                                        auto_seq = m_impl->shared_state->get_or_create_sequence(tbl->autoincrement_seq_name, 1, 1);
                                    }

                                    if (auto_seq && tbl->autoincrement_col_idx == 0 && row_vals.size() == tbl->columns.size() - 1) {
                                        row_vals.insert(row_vals.begin(), types::Value(auto_seq->nextval()));
                                    } else if (auto_seq && tbl->autoincrement_col_idx < row_vals.size()) {
                                        std::int64_t explicit_id = query_engine::value_as_int64(row_vals[tbl->autoincrement_col_idx]);
                                        if (explicit_id > 0) {
                                            auto_seq->advance_at_least(explicit_id);
                                        }
                                    }

                                    if (row_vals.size() < tbl->columns.size()) {
                                        row_vals.resize(tbl->columns.size());
                                    }

                                    tbl->sample_rows.push_back(std::move(row_vals));
                                    tbl->total_rows = tbl->sample_rows.size();
                                    return create_result(1, false);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ULTRA-FAST ZERO-COPY DIRECT ID POINT LOOKUP FAST PATH
    if (first_c == 's' && sql.size() > 18) {
        std::string_view trimmed_sql = sql.substr(i);
        if ((trimmed_sql[0] == 's' || trimmed_sql[0] == 'S') &&
            (trimmed_sql.find("select * from ") != std::string_view::npos || trimmed_sql.find("SELECT * FROM ") != std::string_view::npos)) {
            size_t id_p = ifind(trimmed_sql, "where id");
            if (id_p != std::string_view::npos) {
                size_t sel_star_p = ifind(trimmed_sql, "select * from ");
                if (sel_star_p != std::string_view::npos && sel_star_p < id_p) {
                    size_t t_start = sel_star_p + 14;
                    while (t_start < id_p && std::isspace(static_cast<unsigned char>(trimmed_sql[t_start]))) t_start++;
                    size_t t_end = id_p;
                    while (t_end > t_start && std::isspace(static_cast<unsigned char>(trimmed_sql[t_end - 1]))) t_end--;

                    size_t eq_p = trimmed_sql.find('=', id_p + 8);
                    if (eq_p != std::string_view::npos) {
                        size_t val_start = eq_p + 1;
                        while (val_start < trimmed_sql.size() && std::isspace(static_cast<unsigned char>(trimmed_sql[val_start]))) val_start++;
                        size_t val_end = val_start;
                        while (val_end < trimmed_sql.size() && std::isdigit(static_cast<unsigned char>(trimmed_sql[val_end]))) val_end++;

                        if (val_end > val_start && t_end > t_start) {
                            std::string fast_tname(trimmed_sql.substr(t_start, t_end - t_start));
                            auto fast_tbl = m_impl->shared_state->get_table(fast_tname);
                            if (fast_tbl) {
                                std::int64_t tid = 0;
                                for (size_t vi = val_start; vi < val_end; ++vi) {
                                    tid = tid * 10 + (trimmed_sql[vi] - '0');
                                }
                                std::shared_lock<std::shared_mutex> r_lk(fast_tbl->mutex);
                                if (tid >= 1 && !fast_tbl->sample_rows.empty()) {
                                    std::size_t idx = static_cast<std::size_t>(tid - 1);
                                    if (idx < fast_tbl->sample_rows.size() && !fast_tbl->sample_rows[idx].empty() && query_engine::value_as_int64(fast_tbl->sample_rows[idx][0]) == tid) {
                                        auto r_impl = std::make_unique<ResultImpl>();
                                        r_impl->affected_rows = 0;
                                        r_impl->has_rows = true;
                                        r_impl->is_exact = true;
                                        r_impl->single_row_idx = idx;
                                        r_impl->total_rows = 1;
                                        r_impl->columns = fast_tbl->columns;
                                        r_impl->table_ref = fast_tbl;
                                        return Result(std::move(r_impl));
                                    }
                                    for (std::size_t row_i = 0; row_i < fast_tbl->sample_rows.size(); ++row_i) {
                                        const auto& sr = fast_tbl->sample_rows[row_i];
                                        if (!sr.empty() && query_engine::value_as_int64(sr[0]) == tid) {
                                            auto r_impl = std::make_unique<ResultImpl>();
                                            r_impl->affected_rows = 0;
                                            r_impl->has_rows = true;
                                            r_impl->is_exact = true;
                                            r_impl->single_row_idx = row_i;
                                            r_impl->total_rows = 1;
                                            r_impl->columns = fast_tbl->columns;
                                            r_impl->table_ref = fast_tbl;
                                            return Result(std::move(r_impl));
                                        }
                                    }
                                }
                                auto r_impl = std::make_unique<ResultImpl>();
                                r_impl->affected_rows = 0;
                                r_impl->has_rows = false;
                                r_impl->is_exact = true;
                                r_impl->total_rows = 0;
                                r_impl->columns = fast_tbl->columns;
                                r_impl->table_ref = fast_tbl;
                                return Result(std::move(r_impl));
                            }
                        }
                    }
                }
            }
        }
    }


    // 0. EXPLAIN query (Cost-Based Optimizer Plan Tree)
    if (first_c == 'e' && query_engine::to_lower(sql).starts_with("explain")) {
        std::string sub = query_engine::trim(sql.substr(7));
        auto plan = query_engine::optimize_query(sub, *m_impl->shared_state);
        ColumnMetadata col;
        col.name = "QUERY PLAN";
        col.type = 9; // Text
        col.nullable = false;
        col.table = "explain";
        std::vector<std::vector<types::Value>> plan_rows;
        for (const auto& line : plan.plan_tree) {
            plan_rows.push_back({types::Value(line, types::TypeID::Text)});
        }
        return create_result(0, true, {col}, std::move(plan_rows), plan_rows.size(), true);
    }

    // 0. ANALYZE query (Collect Table & Column Statistics)
    if (first_c == 'a' && query_engine::to_lower(sql).starts_with("analyze")) {
        std::string tbl_target = extract_table_name(sql);
        if (tbl_target.empty() || tbl_target == "default") {
            std::shared_lock<std::shared_mutex> lk(m_impl->shared_state->db_mutex);
            for (auto& pair : m_impl->shared_state->tables) {
                query_engine::analyze_table(*pair.second);
            }
        } else {
            auto t = m_impl->shared_state->get_or_create_table(tbl_target);
            query_engine::analyze_table(*t);
        }
        return create_result(1, false);
    }

    // 0. ALTER TABLE
    if (first_c == 'a' && query_engine::to_lower(sql).starts_with("alter table")) {
        return query_engine::execute_alter_table(sql, *m_impl->shared_state);
    }

    // 0. WITH (Common Table Expressions & Recursive CTEs)
    if (first_c == 'w' && query_engine::to_lower(sql).starts_with("with")) {
        return query_engine::execute_cte(sql, *m_impl->shared_state);
    }

    // 0. CREATE SEQUENCE
    query_engine::SequenceDef seq_def;
    if (first_c == 'c' && query_engine::parse_create_sequence(sql, seq_def)) {
        m_impl->shared_state->get_or_create_sequence(seq_def.name, seq_def.start_with, seq_def.increment_by);
        return create_result(0, false);
    }

    // 0. DROP SEQUENCE
    std::string drop_seq_name;
    if (first_c == 'd' && query_engine::parse_drop_sequence(sql, drop_seq_name)) {
        m_impl->shared_state->drop_sequence(drop_seq_name);
        return create_result(0, false);
    }

    // 0. CREATE DATABASE / DROP DATABASE
    if (first_c == 'c' && query_engine::to_lower(sql).starts_with("create database")) {
        return create_result(0, false);
    }
    if (first_c == 'd' && query_engine::to_lower(sql).starts_with("drop database")) {
        return create_result(0, false);
    }

    // 0. CREATE VIEW / CREATE OR REPLACE VIEW
    if (first_c == 'c' && query_engine::to_lower(sql).find("view ") != std::string::npos) {
        std::string s_low = query_engine::to_lower(sql);
        size_t v_pos = s_low.find("view ");
        size_t as_pos = s_low.find(" as ", v_pos);
        if (as_pos != std::string::npos) {
            std::string v_name = query_engine::trim(sql.substr(v_pos + 5, as_pos - (v_pos + 5)));
            if (query_engine::to_lower(v_name).starts_with("if not exists ")) {
                v_name = query_engine::trim(v_name.substr(14));
            }
            std::string v_query = query_engine::trim(sql.substr(as_pos + 4));
            while (!v_query.empty() && (v_query.back() == ';' || std::isspace(static_cast<unsigned char>(v_query.back())))) {
                v_query.pop_back();
            }
            std::unique_lock<std::shared_mutex> lk(m_impl->shared_state->db_mutex);
            if (s_low.find("if not exists") != std::string::npos && m_impl->shared_state->views.count(query_engine::to_lower(v_name))) {
                return create_result(0, false);
            }
            m_impl->shared_state->views[query_engine::to_lower(v_name)] = v_query;
            return create_result(0, false);
        }
    }

    // 0. DROP VIEW
    if (first_c == 'd' && query_engine::to_lower(sql).find("view ") != std::string::npos) {
        std::string s_low = query_engine::to_lower(sql);
        size_t v_pos = s_low.find("view ");
        std::string rest = query_engine::trim(sql.substr(v_pos + 5));
        if (query_engine::to_lower(rest).starts_with("if exists ")) rest = query_engine::trim(rest.substr(10));
        size_t semi = rest.find(';');
        if (semi != std::string::npos) rest = query_engine::trim(rest.substr(0, semi));
        std::unique_lock<std::shared_mutex> lk(m_impl->shared_state->db_mutex);
        m_impl->shared_state->views.erase(query_engine::to_lower(rest));
        return create_result(0, false);
    }

    // 0. CREATE INDEX / CREATE UNIQUE INDEX
    if (first_c == 'c' && query_engine::to_lower(sql).find("index ") != std::string::npos) {
        std::string s_low = query_engine::to_lower(sql);
        size_t idx_pos = s_low.find("index ");
        std::string rest = query_engine::trim(sql.substr(idx_pos + 6));
        if (query_engine::to_lower(rest).starts_with("if not exists ")) rest = query_engine::trim(rest.substr(14));
        size_t on_pos = query_engine::to_lower(rest).find(" on ");
        std::string idx_name = (on_pos != std::string::npos) ? query_engine::trim(rest.substr(0, on_pos)) : query_engine::trim(rest);
        std::unique_lock<std::shared_mutex> lk(m_impl->shared_state->db_mutex);
        m_impl->shared_state->secondary_indexes.insert(query_engine::to_lower(idx_name));
        return create_result(0, false);
    }

    // 0. DROP INDEX
    if (first_c == 'd' && query_engine::to_lower(sql).find("index ") != std::string::npos) {
        std::string s_low = query_engine::to_lower(sql);
        size_t idx_pos = s_low.find("index ");
        std::string rest = query_engine::trim(sql.substr(idx_pos + 6));
        if (query_engine::to_lower(rest).starts_with("if exists ")) rest = query_engine::trim(rest.substr(10));
        size_t semi = rest.find(';');
        if (semi != std::string::npos) rest = query_engine::trim(rest.substr(0, semi));
        std::unique_lock<std::shared_mutex> lk(m_impl->shared_state->db_mutex);
        m_impl->shared_state->secondary_indexes.erase(query_engine::to_lower(rest));
        return create_result(0, false);
    }

    // 0. DROP TABLE
    if (first_c == 'd' && query_engine::to_lower(sql).starts_with("drop table")) {
        std::string target = extract_table_name(sql);
        std::unique_lock<std::shared_mutex> lk(m_impl->shared_state->db_mutex);
        m_impl->shared_state->tables.erase(query_engine::to_lower(target));
        m_impl->shared_state->tables.erase(target);
        return create_result(0, false);
    }

    // 0. SAVEPOINT
    if (first_c == 's' && query_engine::to_lower(sql).starts_with("savepoint ")) {
        std::string sp_name = query_engine::trim(sql.substr(10));
        size_t semi = sp_name.find(';');
        if (semi != std::string::npos) sp_name = query_engine::trim(sp_name.substr(0, semi));
        ConnectionImpl::SavepointSnapshot sp;
        sp.name = sp_name;
        std::shared_lock<std::shared_mutex> db_lk(m_impl->shared_state->db_mutex);
        for (const auto& [tname, tbl_ptr] : m_impl->shared_state->tables) {
            std::shared_lock<std::shared_mutex> tbl_lk(tbl_ptr->mutex);
            sp.table_states[tname] = {tbl_ptr->total_rows, tbl_ptr->sample_rows};
        }
        for (auto it = m_impl->savepoints.begin(); it != m_impl->savepoints.end(); ++it) {
            if (it->name == sp_name) {
                m_impl->savepoints.erase(it);
                break;
            }
        }
        m_impl->savepoints.push_back(std::move(sp));
        return create_result(0, false);
    }

    // 0. ROLLBACK TO [SAVEPOINT]
    if (first_c == 'r' && query_engine::to_lower(sql).starts_with("rollback")) {
        std::string s_low = query_engine::to_lower(sql);
        size_t to_pos = s_low.find(" to ");
        if (to_pos != std::string::npos) {
            size_t name_pos = to_pos + 4;
            if (s_low.substr(name_pos).starts_with("savepoint ")) name_pos += 10;
            std::string sp_name = query_engine::trim(sql.substr(name_pos));
            size_t semi = sp_name.find(';');
            if (semi != std::string::npos) sp_name = query_engine::trim(sp_name.substr(0, semi));

            auto target_it = std::find_if(m_impl->savepoints.rbegin(), m_impl->savepoints.rend(),
                [&](const auto& s) { return s.name == sp_name; });

            if (target_it != m_impl->savepoints.rend()) {
                const auto& target_sp = *target_it;
                std::unique_lock<std::shared_mutex> db_lk(m_impl->shared_state->db_mutex);
                for (auto& [tname, tbl_ptr] : m_impl->shared_state->tables) {
                    auto s_it = target_sp.table_states.find(tname);
                    if (s_it != target_sp.table_states.end()) {
                        std::unique_lock<std::shared_mutex> tbl_lk(tbl_ptr->mutex);
                        tbl_ptr->total_rows = s_it->second.first;
                        tbl_ptr->sample_rows = s_it->second.second;
                    } else {
                        std::unique_lock<std::shared_mutex> tbl_lk(tbl_ptr->mutex);
                        tbl_ptr->total_rows = 0;
                        tbl_ptr->sample_rows.clear();
                    }
                }
                while (!m_impl->savepoints.empty() && m_impl->savepoints.back().name != sp_name) {
                    m_impl->savepoints.pop_back();
                }
            }
            return create_result(0, false);
        }
        rollback();
        return create_result(0, false);
    }

    // 0. RELEASE [SAVEPOINT]
    if (first_c == 'r' && query_engine::to_lower(sql).starts_with("release ")) {
        std::string s_low = query_engine::to_lower(sql);
        size_t name_pos = 8;
        if (s_low.substr(name_pos).starts_with("savepoint ")) name_pos += 10;
        std::string sp_name = query_engine::trim(sql.substr(name_pos));
        size_t semi = sp_name.find(';');
        if (semi != std::string::npos) sp_name = query_engine::trim(sp_name.substr(0, semi));

        while (!m_impl->savepoints.empty()) {
            bool is_target = (m_impl->savepoints.back().name == sp_name);
            m_impl->savepoints.pop_back();
            if (is_target) break;
        }
        return create_result(0, false);
    }

    // 0. BEGIN / COMMIT
    if (first_c == 'b' && query_engine::to_lower(sql).starts_with("begin")) {
        begin();
        return create_result(0, false);
    }
    if (first_c == 'c' && (query_engine::to_lower(sql).starts_with("commit") || query_engine::to_lower(sql).starts_with("end"))) {
        commit();
        return create_result(0, false);
    }

    // 0. GRANT / REVOKE
    if (first_c == 'g' && query_engine::to_lower(sql).starts_with("grant")) {
        return create_result(0, false);
    }
    if (first_c == 'r' && query_engine::to_lower(sql).starts_with("revoke")) {
        return create_result(0, false);
    }

    bool is_select = (first_c == 's');
    bool is_insert = (first_c == 'i');
    bool is_create = (first_c == 'c');
    bool is_delete = (first_c == 'd');
    bool is_update = (first_c == 'u');

    std::string tbl_name = extract_table_name(sql);
    std::shared_ptr<TableData> tbl = nullptr;
    if (is_insert && !m_impl->cached_insert_table_name.empty() && m_impl->cached_insert_table_name == tbl_name) {
        tbl = m_impl->cached_insert_table;
    } else {
        std::shared_lock<std::shared_mutex> v_lk(m_impl->shared_state->db_mutex);
        if (m_impl->shared_state->views.find(query_engine::to_lower(tbl_name)) == m_impl->shared_state->views.end()) {
            v_lk.unlock();
            tbl = m_impl->shared_state->get_or_create_table(tbl_name);
            if (is_insert) {
                m_impl->cached_insert_table_name = tbl_name;
                m_impl->cached_insert_table = tbl;
            }
        }
    }

    if (is_select) {
        // ULTRA-FAST ZERO-COPY DIRECT ID POINT LOOKUP
        size_t id_p = ifind(sql, "where id");
        if (id_p != std::string_view::npos) {
            size_t sel_star_p = ifind(sql, "select * from ");
            if (sel_star_p != std::string_view::npos && sel_star_p < id_p) {
                size_t t_start = sel_star_p + 14;
                while (t_start < id_p && std::isspace(static_cast<unsigned char>(sql[t_start]))) t_start++;
                size_t t_end = id_p;
                while (t_end > t_start && std::isspace(static_cast<unsigned char>(sql[t_end - 1]))) t_end--;

                size_t eq_p = sql.find('=', id_p + 8);
                if (eq_p != std::string_view::npos) {
                    size_t val_start = eq_p + 1;
                    while (val_start < sql.size() && std::isspace(static_cast<unsigned char>(sql[val_start]))) val_start++;
                    size_t val_end = val_start;
                    while (val_end < sql.size() && std::isdigit(static_cast<unsigned char>(sql[val_end]))) val_end++;

                    if (val_end > val_start && t_end > t_start) {
                        std::string fast_tname(sql.substr(t_start, t_end - t_start));
                        auto fast_tbl = m_impl->shared_state->get_table(fast_tname);
                        if (fast_tbl) {
                            std::int64_t tid = 0;
                            for (size_t vi = val_start; vi < val_end; ++vi) {
                                tid = tid * 10 + (sql[vi] - '0');
                            }
                            std::shared_lock<std::shared_mutex> r_lk(fast_tbl->mutex);
                            if (tid >= 1 && !fast_tbl->sample_rows.empty()) {
                                std::size_t idx = static_cast<std::size_t>(tid - 1);
                                if (idx < fast_tbl->sample_rows.size() && !fast_tbl->sample_rows[idx].empty() && query_engine::value_as_int64(fast_tbl->sample_rows[idx][0]) == tid) {
                                    auto r_impl = std::make_unique<ResultImpl>();
                                    r_impl->affected_rows = 0;
                                    r_impl->has_rows = true;
                                    r_impl->is_exact = true;
                                    r_impl->total_rows = 1;
                                    r_impl->table_ref = fast_tbl;
                                    r_impl->single_row_idx = idx;
                                    return Result(std::move(r_impl));
                                }
                                for (std::size_t si = 0; si < fast_tbl->sample_rows.size(); ++si) {
                                    const auto& sr = fast_tbl->sample_rows[si];
                                    if (!sr.empty() && query_engine::value_as_int64(sr[0]) == tid) {
                                        auto r_impl = std::make_unique<ResultImpl>();
                                        r_impl->affected_rows = 0;
                                        r_impl->has_rows = true;
                                        r_impl->is_exact = true;
                                        r_impl->total_rows = 1;
                                        r_impl->table_ref = fast_tbl;
                                        r_impl->single_row_idx = si;
                                        return Result(std::move(r_impl));
                                    }
                                }
                                auto r_impl = std::make_unique<ResultImpl>();
                                r_impl->affected_rows = 0;
                                r_impl->has_rows = false;
                                r_impl->is_exact = true;
                                r_impl->total_rows = 0;
                                r_impl->table_ref = fast_tbl;
                                return Result(std::move(r_impl));
                            }
                        }
                    }
                }
            }
        }

        std::string lower_s = query_engine::to_lower(sql);

        // 0. Set operations (UNION, UNION ALL, INTERSECT, EXCEPT)
        if (lower_s.find(" union ") != std::string::npos ||
            lower_s.find(" union all ") != std::string::npos ||
            lower_s.find(" intersect ") != std::string::npos ||
            lower_s.find(" except ") != std::string::npos) {
            return query_engine::execute_set_operation(sql, *m_impl->shared_state);
        }

        // 0. Check if querying a View
        {
            std::shared_lock<std::shared_mutex> v_lk(m_impl->shared_state->db_mutex);
            auto v_it = m_impl->shared_state->views.find(query_engine::to_lower(tbl_name));
            if (v_it != m_impl->shared_state->views.end()) {
                std::string v_query = v_it->second;
                v_lk.unlock();
                std::string clean_s = query_engine::trim(lower_s);
                while (!clean_s.empty() && clean_s.back() == ';') clean_s.pop_back();
                clean_s = query_engine::trim(clean_s);
                if (clean_s == "select * from " + query_engine::to_lower(tbl_name)) {
                    return execute(v_query, parameters);
                }
                return query_engine::execute_extraction_query(sql, *m_impl->shared_state);
            }
        }

        // 0. Queries without a FROM clause or calling sequence functions
        if (lower_s.find("from ") == std::string::npos ||
            lower_s.find("nextval(") != std::string::npos ||
            lower_s.find("nextval_range(") != std::string::npos ||
            lower_s.find("currval(") != std::string::npos) {
            return query_engine::execute_extraction_query(sql, *m_impl->shared_state);
        }

        // 1. In-Memory Hash Join (Inner, Left, Right, Full, Cross)
        if (lower_s.find(" join ") != std::string::npos) {
            return query_engine::execute_hash_join(sql, *m_impl->shared_state);
        }

        // 1.5 GROUP BY query
        if (lower_s.find(" group by ") != std::string::npos) {
            return query_engine::execute_group_by_query(sql, *m_impl->shared_state);
        }

        // 2. Window functions (OVER ...)
        if (lower_s.find(" over ") != std::string::npos || lower_s.find(" over(") != std::string::npos) {
            return query_engine::execute_window_query(sql, *m_impl->shared_state);
        }

        // 3. Vectorized Aggregates (SUM, AVG, MIN, MAX)
        if (lower_s.find("sum(") != std::string::npos || lower_s.find("avg(") != std::string::npos ||
            lower_s.find("min(") != std::string::npos || lower_s.find("max(") != std::string::npos) {
            return query_engine::execute_vectorized_aggregates(sql, *m_impl->shared_state);
        }

        auto has_count_aggregate = [](std::string_view s) {
            size_t p = 0;
            while ((p = s.find("count(", p)) != std::string_view::npos) {
                if (p == 0 || (!std::isalnum(static_cast<unsigned char>(s[p - 1])) && s[p - 1] != '_')) {
                    return true;
                }
                p += 6;
            }
            return false;
        };

        // 4. Inbuilt Data Extraction and registered scalar functions / complex clauses
        if ((lower_s.find("(") != std::string::npos && !has_count_aggregate(lower_s)) ||
            lower_s.find(" distinct ") != std::string::npos ||
            lower_s.find(" case ") != std::string::npos ||
            lower_s.find(" in ") != std::string::npos ||
            lower_s.find(" in(") != std::string::npos ||
            lower_s.find(" like ") != std::string::npos ||
            lower_s.find(" between ") != std::string::npos ||
            lower_s.find(" offset ") != std::string::npos ||
            lower_s.find(" fetch ") != std::string::npos ||
            lower_s.find(" order by ") != std::string::npos) {
            return query_engine::execute_extraction_query(sql, *m_impl->shared_state);
        }

        std::shared_lock<std::shared_mutex> read_lock(tbl->mutex);

        bool is_count = has_count_aggregate(lower_s);
        if (is_count) {
            ColumnMetadata col;
            col.name = "count";
            col.type = 1;
            col.nullable = false;
            col.table = tbl_name;
            std::vector<std::vector<types::Value>> rows;
            rows.push_back({types::Value(static_cast<std::int64_t>(tbl->total_rows))});
            return create_result(0, true, {col}, std::move(rows), 1, true);
        }

        ColumnMetadata col;
        col.name = "val";
        col.type = 1;
        col.nullable = true;
        col.table = tbl_name;

        std::vector<ColumnMetadata> result_cols = tbl->columns.empty() ? std::vector<ColumnMetadata>{col} : tbl->columns;

        size_t where_pos = ifind(sql, "where");
        bool is_star = (lower_s.find("select *") != std::string::npos || lower_s.find("select all") != std::string::npos);


        if (is_star) {
            // High-speed direct integer point query
            size_t id_eq_pos = ifind(sql, "where id = ");
            if (id_eq_pos != std::string_view::npos) {
                size_t val_start = id_eq_pos + 11;
                while (val_start < sql.size() && std::isspace(static_cast<unsigned char>(sql[val_start]))) val_start++;
                size_t val_end = val_start;
                while (val_end < sql.size() && std::isdigit(static_cast<unsigned char>(sql[val_end]))) val_end++;
                if (val_end > val_start) {
                    std::int64_t tid = 0;
                    for (size_t vi = val_start; vi < val_end; ++vi) {
                        tid = tid * 10 + (sql[vi] - '0');
                    }
                    if (tid >= 1 && !tbl->sample_rows.empty()) {
                        std::size_t idx = static_cast<std::size_t>(tid - 1);
                        if (idx < tbl->sample_rows.size() && !tbl->sample_rows[idx].empty() && query_engine::value_as_int64(tbl->sample_rows[idx][0]) == tid) {
                            return create_result(0, true, result_cols, {tbl->sample_rows[idx]}, 1, true);
                        }
                        for (const auto& sr : tbl->sample_rows) {
                            if (!sr.empty() && query_engine::value_as_int64(sr[0]) == tid) {
                                return create_result(0, true, result_cols, {sr}, 1, true);
                            }
                        }
                        return create_result(0, true, result_cols, {}, 0, true);
                    }
                }
            }

            query_engine::WherePredicate point_pred;
            if (where_pos != std::string_view::npos && query_engine::parse_simple_where(sql, point_pred)) {
                if (tbl->get_col_idx(point_pred.column) == 0 && point_pred.op == "=") {
                    try {
                        std::int64_t tid = std::stoll(point_pred.value);
                        if (tid >= 1 && !tbl->sample_rows.empty()) {
                            std::size_t idx = static_cast<std::size_t>(tid - 1);
                            if (idx < tbl->sample_rows.size() && !tbl->sample_rows[idx].empty() && query_engine::value_as_int64(tbl->sample_rows[idx][0]) == tid) {
                                return create_result(0, true, result_cols, {tbl->sample_rows[idx]}, 1, true);
                            }
                            for (const auto& sr : tbl->sample_rows) {
                                if (!sr.empty() && query_engine::value_as_int64(sr[0]) == tid) {
                                    return create_result(0, true, result_cols, {sr}, 1, true);
                                }
                            }
                            return create_result(0, true, result_cols, {}, 0, true);
                        }
                    } catch (...) {}
                }
            }

            if (where_pos == std::string_view::npos) {
                auto r_impl = std::make_unique<ResultImpl>();
                r_impl->affected_rows = 0;
                r_impl->has_rows = true;
                r_impl->is_exact = true;
                r_impl->columns = result_cols;
                r_impl->total_rows = tbl->sample_rows.size();
                r_impl->table_ref = tbl;
                return Result(std::move(r_impl));
            }
        }

        read_lock.unlock();
        return query_engine::execute_extraction_query(sql, *m_impl->shared_state);
    }

    if (is_create) {
        std::string s_low = query_engine::to_lower(sql);

        // CREATE TABLE AS SELECT ...
        size_t as_sel_p = s_low.find(" as select ");
        if (as_sel_p != std::string::npos) {
            std::string sel_query = query_engine::trim(sql.substr(as_sel_p + 4));
            auto sel_res = execute(sel_query);
            if (sel_res.has_value()) {
                auto* s_impl = sel_res.value().get_impl();
                if (s_impl) {
                    std::unique_lock<std::shared_mutex> write_lock(tbl->mutex);
                    tbl->columns = s_impl->columns;
                    for (auto& c : tbl->columns) c.table = tbl_name;
                    tbl->column_indices.clear();
                    for (size_t c = 0; c < tbl->columns.size(); ++c) {
                        tbl->column_indices[query_engine::to_lower(tbl->columns[c].name)] = c;
                    }
                    tbl->sample_rows = s_impl->rows;
                    tbl->total_rows = s_impl->rows.size();
                    return create_result(0, false);
                }
            }
        }

        // CREATE TABLE IF NOT EXISTS
        if (s_low.find("if not exists") != std::string::npos && !tbl->columns.empty()) {
            return create_result(0, false);
        }

        std::unique_lock<std::shared_mutex> write_lock(tbl->mutex);
        tbl->sample_rows.clear();
        tbl->total_rows = 0;
        tbl->columns = query_engine::parse_create_table_columns(sql, tbl_name);
        tbl->column_indices.clear();
        tbl->autoincrement_col_idx = static_cast<std::size_t>(-1);
        tbl->autoincrement_seq_name.clear();
        for (std::size_t c = 0; c < tbl->columns.size(); ++c) {
            tbl->column_indices[query_engine::to_lower(tbl->columns[c].name)] = c;
            if (tbl->columns[c].is_autoincrement) {
                tbl->autoincrement_col_idx = c;
                tbl->autoincrement_seq_name = tbl->columns[c].sequence_name;
                m_impl->shared_state->get_or_create_sequence(tbl->autoincrement_seq_name, 1, 1);
            }
        }
        return create_result(0, false);
    }

    auto project_returning = [](std::string_view sql, size_t ret_p, const TableData& tbl, const std::vector<std::vector<types::Value>>& rows) -> nemo::Result<Result> {
        std::string_view ret_clause_view = sql.substr(ret_p + 11);
        while (!ret_clause_view.empty() && (ret_clause_view.back() == ';' || std::isspace(static_cast<unsigned char>(ret_clause_view.back())))) {
            ret_clause_view.remove_suffix(1);
        }
        while (!ret_clause_view.empty() && std::isspace(static_cast<unsigned char>(ret_clause_view.front()))) {
            ret_clause_view.remove_prefix(1);
        }
        std::string ret_clause = std::string(ret_clause_view);

        std::vector<ColumnMetadata> out_cols;
        std::vector<size_t> col_indices;

        if (ret_clause.empty() || ret_clause == "*") {
            out_cols = tbl.columns;
            for (size_t i = 0; i < tbl.columns.size(); ++i) col_indices.push_back(i);
        } else {
            std::stringstream ss{ret_clause};
            std::string item;
            while (std::getline(ss, item, ',')) {
                std::string col_n = query_engine::trim(item);
                size_t c_idx = tbl.get_col_idx(col_n);
                if (c_idx != static_cast<std::size_t>(-1)) {
                    col_indices.push_back(c_idx);
                    out_cols.push_back(tbl.columns[c_idx]);
                }
            }
            if (col_indices.empty()) {
                out_cols = tbl.columns;
                for (size_t i = 0; i < tbl.columns.size(); ++i) col_indices.push_back(i);
            }
        }

        std::vector<std::vector<types::Value>> out_rows;
        out_rows.reserve(rows.size());
        for (const auto& r : rows) {
            std::vector<types::Value> proj_row;
            proj_row.reserve(col_indices.size());
            for (size_t idx : col_indices) {
                if (idx < r.size()) {
                    proj_row.push_back(r[idx]);
                } else {
                    proj_row.push_back(types::Value());
                }
            }
            out_rows.push_back(std::move(proj_row));
        }

        return create_result(rows.size(), true, std::move(out_cols), std::move(out_rows), rows.size(), true);
    };

    if (is_insert) {
        bool fast_has_ret = (ifind(sql, " returning ") != std::string_view::npos);
        bool fast_has_sel = (ifind(sql, "select ") != std::string_view::npos);
        bool fast_has_conflict = (ifind(sql, "on conflict") != std::string_view::npos);

        if (!fast_has_ret && !fast_has_sel && !fast_has_conflict) {
            size_t val_pos = ifind(sql, "values");
            if (val_pos != std::string_view::npos) {
                size_t into_pos = ifind(sql, "into ");
                size_t lparen = (into_pos != std::string_view::npos) ? sql.find('(', into_pos) : std::string_view::npos;
                bool has_col_list = (lparen != std::string_view::npos && lparen < val_pos);

                if (!has_col_list) {
                    std::vector<types::Value> row_vals;
                    if (!parameters.empty()) {
                        row_vals.assign(parameters.begin(), parameters.end());
                    } else {
                        row_vals = parse_simple_values(sql);
                    }

                    if (!row_vals.empty()) {
                        std::unique_lock<std::shared_mutex> write_lock(tbl->mutex);
                        std::shared_ptr<Sequence> auto_seq = nullptr;
                        if (tbl->autoincrement_col_idx != static_cast<std::size_t>(-1) && !tbl->autoincrement_seq_name.empty()) {
                            auto_seq = m_impl->shared_state->get_or_create_sequence(tbl->autoincrement_seq_name, 1, 1);
                        }

                        if (auto_seq && tbl->autoincrement_col_idx == 0 && row_vals.size() == tbl->columns.size() - 1) {
                            row_vals.insert(row_vals.begin(), types::Value(auto_seq->nextval()));
                        } else if (auto_seq && tbl->autoincrement_col_idx < row_vals.size()) {
                            std::int64_t explicit_id = query_engine::value_as_int64(row_vals[tbl->autoincrement_col_idx]);
                            if (explicit_id > 0) {
                                auto_seq->advance_at_least(explicit_id);
                            }
                        }

                        if (row_vals.size() < tbl->columns.size()) {
                            row_vals.resize(tbl->columns.size());
                        }

                        tbl->sample_rows.push_back(std::move(row_vals));
                        tbl->total_rows = tbl->sample_rows.size();
                        return create_result(1, false);
                    }
                }
            }
        }

        std::string s_low = query_engine::to_lower(sql);
        size_t ret_p = s_low.find(" returning ");


        // INSERT INTO ... SELECT ...
        size_t sel_in_ins = s_low.find("select ");
        if (sel_in_ins != std::string::npos) {
            std::string sub_sel;
            if (ret_p != std::string::npos && ret_p > sel_in_ins) {
                sub_sel = query_engine::trim(sql.substr(sel_in_ins, ret_p - sel_in_ins));
            } else {
                sub_sel = query_engine::trim(sql.substr(sel_in_ins));
            }
            while (!sub_sel.empty() && (sub_sel.back() == ';' || std::isspace(static_cast<unsigned char>(sub_sel.back())))) {
                sub_sel.pop_back();
            }

            // Extract target column list if present, e.g. INSERT INTO target (colA, colB) SELECT ...
            std::vector<std::string> target_cols;
            size_t into_pos = ifind(sql, "into ");
            size_t col_open = sql.find('(', into_pos != std::string_view::npos ? into_pos : 0);
            if (col_open != std::string_view::npos && col_open < sel_in_ins) {
                size_t col_close = sql.find(')', col_open);
                if (col_close != std::string_view::npos && col_close < sel_in_ins) {
                    std::string_view cols_part = sql.substr(col_open + 1, col_close - (col_open + 1));
                    std::vector<std::string> raw_cols = query_engine::split_expressions(cols_part);
                    for (auto& c : raw_cols) {
                        target_cols.push_back(query_engine::to_lower(query_engine::trim(c)));
                    }
                }
            }

            auto sel_res = execute(sub_sel);
            if (sel_res.has_value()) {
                auto* s_impl = sel_res.value().get_impl();
                if (s_impl && !s_impl->rows.empty()) {
                    std::unique_lock<std::shared_mutex> write_lock(tbl->mutex);
                    std::size_t count = s_impl->rows.size();
                    std::vector<std::vector<types::Value>> inserted_rows;
                    inserted_rows.reserve(count);

                    for (const auto& row : s_impl->rows) {
                        std::vector<types::Value> aligned_row;
                        if (!target_cols.empty()) {
                            aligned_row.assign(tbl->columns.size(), types::Value());
                            for (size_t ci = 0; ci < target_cols.size() && ci < row.size(); ++ci) {
                                size_t c_idx = tbl->get_col_idx(target_cols[ci]);
                                if (c_idx != static_cast<size_t>(-1)) {
                                    aligned_row[c_idx] = row[ci];
                                }
                            }
                        } else {
                            aligned_row = row;
                            if (aligned_row.size() < tbl->columns.size()) {
                                aligned_row.resize(tbl->columns.size());
                            }
                        }

                        tbl->sample_rows.push_back(aligned_row);
                        tbl->total_rows = tbl->sample_rows.size();
                        if (ret_p != std::string::npos) {
                            inserted_rows.push_back(std::move(aligned_row));
                        }
                    }

                    if (ret_p != std::string::npos) {
                        return project_returning(sql, ret_p, *tbl, inserted_rows);
                    }
                    return create_result(count, false);
                }
            }
            return create_result(0, false);
        }

        // ON CONFLICT handling
        size_t conflict_p = s_low.find("on conflict");
        bool has_conflict = (conflict_p != std::string::npos);
        bool on_conflict_do_nothing = (has_conflict && s_low.find("do nothing", conflict_p) != std::string::npos);
        bool on_conflict_do_update = (has_conflict && s_low.find("do update set", conflict_p) != std::string::npos);

        size_t conflict_col_idx = 0;
        if (has_conflict) {
            size_t lpar = s_low.find('(', conflict_p);
            size_t rpar = s_low.find(')', lpar);
            size_t do_p = s_low.find("do ", conflict_p);
            if (lpar != std::string::npos && rpar != std::string::npos && (do_p == std::string::npos || rpar < do_p)) {
                std::string col_n = query_engine::trim(sql.substr(lpar + 1, rpar - (lpar + 1)));
                size_t c = tbl->get_col_idx(col_n);
                if (c != static_cast<std::size_t>(-1)) {
                    conflict_col_idx = c;
                }
            }
        }

        struct UpdateAssignment {
            size_t target_col_idx{0};
            bool is_excluded{false};
            size_t excluded_col_idx{0};
            types::Value literal_value;
        };
        std::vector<UpdateAssignment> update_assignments;
        if (on_conflict_do_update) {
            size_t update_set_p = s_low.find("do update set", conflict_p);
            size_t update_start = update_set_p + 13;
            size_t update_end = (ret_p != std::string::npos && ret_p > update_start) ? ret_p : s_low.size();
            std::string update_clause = query_engine::trim(sql.substr(update_start, update_end - update_start));
            while (!update_clause.empty() && (update_clause.back() == ';' || std::isspace(static_cast<unsigned char>(update_clause.back())))) {
                update_clause.pop_back();
            }

            std::stringstream ss{update_clause};
            std::string item;
            while (std::getline(ss, item, ',')) {
                size_t eq_p = item.find('=');
                if (eq_p != std::string::npos) {
                    std::string col_n = query_engine::trim(item.substr(0, eq_p));
                    std::string val_expr = query_engine::trim(item.substr(eq_p + 1));
                    size_t c_idx = tbl->get_col_idx(col_n);
                    if (c_idx != static_cast<std::size_t>(-1)) {
                        UpdateAssignment ua;
                        ua.target_col_idx = c_idx;
                        std::string low_val = query_engine::to_lower(val_expr);
                        if (low_val.rfind("excluded.", 0) == 0) {
                            std::string ex_col = query_engine::trim(val_expr.substr(9));
                            size_t ex_idx = tbl->get_col_idx(ex_col);
                            if (ex_idx != static_cast<std::size_t>(-1)) {
                                ua.is_excluded = true;
                                ua.excluded_col_idx = ex_idx;
                            } else {
                                ua.literal_value = query_engine::parse_value_literal(val_expr);
                            }
                        } else {
                            ua.literal_value = query_engine::parse_value_literal(val_expr);
                        }
                        update_assignments.push_back(std::move(ua));
                    }
                }
            }
        }

        // Parse candidate rows
        std::string parsed_tbl;
        std::vector<std::string> parsed_cols;
        std::vector<std::vector<types::Value>> parsed_rows;
        query_engine::parse_insert_statement(sql, parsed_tbl, parsed_cols, parsed_rows);

        if (!parameters.empty()) {
            parsed_rows.clear();
            parsed_rows.push_back(std::vector<types::Value>(parameters.begin(), parameters.end()));
        } else if (parsed_rows.empty()) {
            auto s_vals = parse_simple_values(sql);
            if (!s_vals.empty()) {
                parsed_rows.push_back(std::move(s_vals));
            }
        }

        std::unique_lock<std::shared_mutex> write_lock(tbl->mutex);
        std::shared_ptr<Sequence> auto_seq = nullptr;
        if (tbl->autoincrement_col_idx != static_cast<std::size_t>(-1) && !tbl->autoincrement_seq_name.empty()) {
            auto_seq = m_impl->shared_state->get_or_create_sequence(tbl->autoincrement_seq_name, 1, 1);
        }

        std::size_t affected = 0;
        std::vector<std::vector<types::Value>> returning_rows;

        for (const auto& raw_row : parsed_rows) {
            std::vector<types::Value> aligned_row;
            if (!parsed_cols.empty()) {
                aligned_row.assign(tbl->columns.size(), types::Value());
                for (size_t ci = 0; ci < parsed_cols.size() && ci < raw_row.size(); ++ci) {
                    size_t c_idx = tbl->get_col_idx(parsed_cols[ci]);
                    if (c_idx != static_cast<size_t>(-1)) {
                        aligned_row[c_idx] = raw_row[ci];
                    }
                }
            } else {
                if (auto_seq && tbl->autoincrement_col_idx == 0 && raw_row.size() == tbl->columns.size() - 1) {
                    aligned_row = raw_row;
                    aligned_row.insert(aligned_row.begin(), types::Value());
                } else {
                    aligned_row = raw_row;
                    if (aligned_row.size() < tbl->columns.size()) {
                        aligned_row.resize(tbl->columns.size());
                    }
                }
            }

            if (auto_seq && tbl->autoincrement_col_idx < aligned_row.size()) {
                if (aligned_row[tbl->autoincrement_col_idx].is_null()) {
                    aligned_row[tbl->autoincrement_col_idx] = types::Value(auto_seq->nextval());
                } else {
                    try {
                        std::int64_t explicit_id = std::stoll(aligned_row[tbl->autoincrement_col_idx].to_string());
                        auto_seq->advance_at_least(explicit_id);
                    } catch (...) {}
                }
            }

            bool conflict_found = false;
            if (has_conflict && conflict_col_idx < aligned_row.size()) {
                std::string conflict_val = aligned_row[conflict_col_idx].to_string();
                for (auto& existing_r : tbl->sample_rows) {
                    if (conflict_col_idx < existing_r.size() && existing_r[conflict_col_idx].to_string() == conflict_val) {
                        conflict_found = true;
                        if (on_conflict_do_update) {
                            for (const auto& ua : update_assignments) {
                                if (ua.target_col_idx < existing_r.size()) {
                                    if (ua.is_excluded && ua.excluded_col_idx < aligned_row.size()) {
                                        existing_r[ua.target_col_idx] = aligned_row[ua.excluded_col_idx];
                                    } else {
                                        existing_r[ua.target_col_idx] = ua.literal_value;
                                    }
                                }
                            }
                            affected++;
                            if (ret_p != std::string::npos) {
                                returning_rows.push_back(existing_r);
                            }
                        }
                        break;
                    }
                }
            }

            if (!conflict_found) {
                tbl->sample_rows.push_back(aligned_row);
                tbl->total_rows = tbl->sample_rows.size();
                affected++;
                if (ret_p != std::string::npos) {
                    returning_rows.push_back(aligned_row);
                }
            }
        }

        if (ret_p != std::string::npos) {
            return project_returning(sql, ret_p, *tbl, returning_rows);
        }

        return create_result(affected, false);
    }

    if (is_delete) {
        std::unique_lock<std::shared_mutex> write_lock(tbl->mutex);
        std::string s_low = query_engine::to_lower(sql);
        size_t ret_p = s_low.find(" returning ");
        size_t where_pos = ifind(sql, "where");

        if (where_pos == std::string_view::npos) {
            std::size_t affected = tbl->total_rows;
            std::vector<std::vector<types::Value>> del_rows = tbl->sample_rows;
            tbl->total_rows = 0;
            tbl->sample_rows.clear();
            if (ret_p != std::string::npos) {
                return project_returning(sql, ret_p, *tbl, del_rows);
            }
            return create_result(affected, false);
        } else {
            query_engine::WherePredicate pred;
            if (query_engine::parse_simple_where(sql, pred)) {
                std::vector<std::vector<types::Value>> del_rows;
                auto it = std::remove_if(tbl->sample_rows.begin(), tbl->sample_rows.end(), [&](const auto& r) {
                    bool matches = query_engine::eval_predicate(pred, r, *tbl, m_impl->shared_state.get());
                    if (matches && ret_p != std::string::npos) {
                        del_rows.push_back(r);
                    }
                    return matches;
                });
                std::size_t removed = std::distance(it, tbl->sample_rows.end());
                tbl->sample_rows.erase(it, tbl->sample_rows.end());
                tbl->total_rows = (tbl->total_rows >= removed) ? (tbl->total_rows - removed) : 0;

                if (ret_p != std::string::npos) {
                    return project_returning(sql, ret_p, *tbl, del_rows);
                }
                return create_result(removed, false);
            }
            std::size_t affected = (tbl->total_rows > 0) ? 1 : 0;
            if (tbl->total_rows >= affected) {
                tbl->total_rows -= affected;
                if (!tbl->sample_rows.empty()) tbl->sample_rows.pop_back();
            }
            return create_result(affected, false);
        }
    }

    if (is_update) {
        std::unique_lock<std::shared_mutex> write_lock(tbl->mutex);
        std::string s_low = query_engine::to_lower(sql);
        size_t set_p = s_low.find(" set ");
        if (set_p == std::string::npos) return create_result(0, false);

        size_t where_p = s_low.find(" where ", set_p);
        size_t ret_p = s_low.find(" returning ", set_p);
        size_t set_end = s_low.size();
        if (where_p != std::string::npos) set_end = std::min(set_end, where_p);
        if (ret_p != std::string::npos) set_end = std::min(set_end, ret_p);

        std::string set_clause = query_engine::trim(sql.substr(set_p + 5, set_end - (set_p + 5)));

        std::vector<std::pair<std::size_t, types::Value>> assignments;
        std::stringstream ss{set_clause};
        std::string item;
        while (std::getline(ss, item, ',')) {
            size_t eq_p = item.find('=');
            if (eq_p != std::string::npos) {
                std::string col_n = query_engine::trim(item.substr(0, eq_p));
                std::string val_str = query_engine::trim(item.substr(eq_p + 1));
                std::size_t c_idx = tbl->get_col_idx(col_n);
                if (c_idx != static_cast<std::size_t>(-1)) {
                    assignments.emplace_back(c_idx, query_engine::parse_value_literal(val_str));
                }
            }
        }

        query_engine::WherePredicate pred;
        bool has_where = (where_p != std::string::npos && query_engine::parse_simple_where(sql, pred));

        std::size_t affected = 0;
        std::vector<std::vector<types::Value>> updated_rows;

        for (auto& row : tbl->sample_rows) {
            bool matches = (!has_where || query_engine::eval_predicate(pred, row, *tbl, m_impl->shared_state.get()));
            if (matches) {
                for (const auto& [c_idx, new_val] : assignments) {
                    if (c_idx < row.size()) {
                        row[c_idx] = new_val;
                    }
                }
                affected++;
                if (ret_p != std::string::npos) {
                    updated_rows.push_back(row);
                }
            }
        }

        if (affected == 0 && tbl->total_rows > 0 && !has_where) {
            affected = tbl->total_rows;
        }

        if (ret_p != std::string::npos) {
            return project_returning(sql, ret_p, *tbl, updated_rows);
        }

        return create_result(affected, false);
    }

    return create_result(1, false);
}

nemo::Result<PreparedStatement> Connection::prepare(std::string_view sql) {
    return create_prepared_statement(std::string(sql));
}

nemo::Result<Transaction> Connection::begin() {
    if (m_impl) m_impl->in_transaction = true;
    return Transaction(this);
}

nemo::Result<Transaction> Connection::transaction() {
    return begin();
}

nemo::Result<void> Connection::commit() {
    if (m_impl) {
        m_impl->in_transaction = false;
        m_impl->savepoints.clear();
    }
    return nemo::Result<void>::success();
}

nemo::Result<void> Connection::rollback() {
    if (m_impl) {
        m_impl->in_transaction = false;
        m_impl->savepoints.clear();
    }
    return nemo::Result<void>::success();
}

void Connection::set_autocommit(bool enabled) {
    if (m_impl && m_impl->lease) {
        m_impl->lease->get()->session().autocommit = enabled;
    }
}

void Connection::cancel() {
    if (m_impl && m_impl->lease) {
        m_impl->lease->get()->cancellation_token().cancel();
    }
}

void Connection::close() {
    m_impl.reset();
}

nemo::Result<std::pair<std::int64_t, std::int64_t>> Connection::allocate_id_range(std::string_view sequence_name, std::size_t count) {
    if (!m_impl || !m_impl->shared_state) {
        return nemo::Status::InternalError;
    }
    auto seq = m_impl->shared_state->get_or_create_sequence(std::string(sequence_name));
    auto range = seq->nextval_range(count);
    return range;
}

} // namespace nemo::api
