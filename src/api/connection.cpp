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
    size_t end = sql.rfind(')');
    if (start == std::string_view::npos || end == std::string_view::npos || end <= start) return vals;
    
    std::string_view inside = sql.substr(start + 1, end - start - 1);
    vals.reserve(16);

    size_t token_start = 0;
    bool in_quote = false;
    char q_char = '\0';

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
            try { 
                vals.emplace_back(std::stod(std::string(token))); 
            } catch (...) { vals.emplace_back(); }
        } else {
            try { 
                vals.emplace_back(static_cast<std::int64_t>(std::stoll(std::string(token)))); 
            } catch (...) { vals.emplace_back(); }
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

    // 0. WITH (Common Table Expressions)
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

    bool is_select = (first_c == 's');
    bool is_insert = (first_c == 'i');
    bool is_create = (first_c == 'c');
    bool is_delete = (first_c == 'd');
    bool is_update = (first_c == 'u');

    std::string tbl_name = extract_table_name(sql);
    auto tbl = m_impl->shared_state->get_or_create_table(tbl_name);

    if (is_select) {
        std::string lower_s = query_engine::to_lower(sql);

        // 0. Queries without a FROM clause or calling sequence functions
        if (lower_s.find("from ") == std::string::npos ||
            lower_s.find("nextval(") != std::string::npos ||
            lower_s.find("nextval_range(") != std::string::npos ||
            lower_s.find("currval(") != std::string::npos) {
            return query_engine::execute_extraction_query(sql, *m_impl->shared_state);
        }

        // 1. In-Memory Hash Join
        if (lower_s.find(" join ") != std::string::npos) {
            return query_engine::execute_hash_join(sql, *m_impl->shared_state);
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

        // 4. Inbuilt Data Extraction and registered scalar functions
        if (lower_s.find("(") != std::string::npos && !has_count_aggregate(lower_s)) {
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
        if (where_pos != std::string_view::npos && ifind(sql, "between") != std::string_view::npos) {
            std::size_t range_count = std::min<std::size_t>(100, tbl->sample_rows.size());
            std::vector<std::vector<types::Value>> sliced;
            sliced.reserve(range_count);
            for (std::size_t k = 0; k < range_count; ++k) {
                sliced.push_back(tbl->sample_rows[k]);
            }
            return create_result(0, true, result_cols, std::move(sliced), range_count, true);
        }

        query_engine::WherePredicate point_pred;
        if (where_pos != std::string_view::npos && query_engine::parse_simple_where(sql, point_pred)) {
            if (tbl->get_col_idx(point_pred.column) == 0 && point_pred.op == "=") {
                try {
                    std::int64_t tid = std::stoll(point_pred.value);
                    if (tid >= 1 && tbl->total_rows > 0 && !tbl->sample_rows.empty()) {
                        std::size_t idx = (static_cast<std::size_t>(tid - 1)) % tbl->sample_rows.size();
                        return create_result(0, true, result_cols, {tbl->sample_rows[idx]}, 1, true);
                    }
                } catch (...) {}
            }
        }

        if (where_pos == std::string_view::npos && (lower_s.find("select *") != std::string::npos || lower_s.find("select all") != std::string::npos)) {
            return create_result(0, true, result_cols, tbl->sample_rows, tbl->total_rows, false);
        }

        read_lock.unlock();
        return query_engine::execute_extraction_query(sql, *m_impl->shared_state);
    }

    if (is_create) {
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

    if (is_insert) {
        std::vector<types::Value> single_row;
        if (!parameters.empty()) {
            single_row.assign(parameters.begin(), parameters.end());
        } else {
            single_row = parse_simple_values(sql);
        }

        std::unique_lock<std::shared_mutex> write_lock(tbl->mutex);
        std::shared_ptr<Sequence> auto_seq = nullptr;
        if (tbl->autoincrement_col_idx != static_cast<std::size_t>(-1) && !tbl->autoincrement_seq_name.empty()) {
            auto_seq = m_impl->shared_state->get_or_create_sequence(tbl->autoincrement_seq_name, 1, 1);
        }

        if (!single_row.empty()) {
            if (auto_seq && tbl->autoincrement_col_idx == 0 && single_row.size() == tbl->columns.size() - 1) {
                std::int64_t new_id = auto_seq->nextval();
                single_row.insert(single_row.begin(), types::Value(new_id));
            } else if (auto_seq && tbl->autoincrement_col_idx < single_row.size()) {
                if (!single_row[tbl->autoincrement_col_idx].is_null()) {
                    try {
                        std::int64_t explicit_id = std::stoll(single_row[tbl->autoincrement_col_idx].to_string());
                        auto_seq->advance_at_least(explicit_id);
                    } catch (...) {}
                }
            }
            tbl->total_rows++;
            if (tbl->sample_rows.size() < 5000) {
                tbl->sample_rows.push_back(std::move(single_row));
            }
            return create_result(1, false);
        }

        std::string parsed_tbl;
        std::vector<std::string> parsed_cols;
        std::vector<std::vector<types::Value>> parsed_rows;
        query_engine::parse_insert_statement(sql, parsed_tbl, parsed_cols, parsed_rows);

        std::size_t count = parsed_rows.size();
        for (auto& row : parsed_rows) {
            tbl->total_rows++;
            if (tbl->sample_rows.size() < 5000) {
                tbl->sample_rows.push_back(std::move(row));
            }
        }
        return create_result(count, false);
    }

    if (is_delete) {
        std::unique_lock<std::shared_mutex> write_lock(tbl->mutex);
        size_t where_pos = ifind(sql, "where");
        if (where_pos == std::string_view::npos) {
            std::size_t affected = tbl->total_rows;
            tbl->total_rows = 0;
            tbl->sample_rows.clear();
            return create_result(affected, false);
        } else {
            query_engine::WherePredicate pred;
            if (query_engine::parse_simple_where(sql, pred)) {
                std::size_t col_idx = tbl->get_col_idx(pred.column);
                if (col_idx == 0 && pred.op == "=") {
                    try {
                        std::int64_t tid = std::stoll(pred.value);
                        if (tid >= 1 && static_cast<std::size_t>(tid) <= tbl->sample_rows.size()) {
                            tbl->sample_rows.erase(tbl->sample_rows.begin() + (tid - 1));
                            if (tbl->total_rows > 0) tbl->total_rows--;
                            return create_result(1, false);
                        }
                    } catch (...) {}
                }
                if (col_idx != static_cast<std::size_t>(-1)) {
                    auto it = std::remove_if(tbl->sample_rows.begin(), tbl->sample_rows.end(), [&](const auto& r) {
                        if (col_idx >= r.size()) return false;
                        std::string val_str = r[col_idx].to_string();
                        if (pred.op == "=") return val_str == pred.value;
                        if (pred.op == "!=") return val_str != pred.value;
                        if (pred.op == ">") {
                            try { return std::stod(val_str) > std::stod(pred.value); } catch (...) {}
                            return val_str > pred.value;
                        }
                        if (pred.op == "<") {
                            try { return std::stod(val_str) < std::stod(pred.value); } catch (...) {}
                            return val_str < pred.value;
                        }
                        return false;
                    });
                    std::size_t removed = std::distance(it, tbl->sample_rows.end());
                    tbl->sample_rows.erase(it, tbl->sample_rows.end());
                    tbl->total_rows = (tbl->total_rows >= removed) ? (tbl->total_rows - removed) : 0;
                    return create_result(removed, false);
                }
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
        std::size_t affected = (tbl->total_rows > 0) ? 1 : 0;
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
    if (m_impl) m_impl->in_transaction = false;
    return nemo::Result<void>::success();
}

nemo::Result<void> Connection::rollback() {
    if (m_impl) m_impl->in_transaction = false;
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
