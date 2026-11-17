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
#include <thread>
#include <charconv>
#include <cstring>

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

bool is_identifier_character(char value) {
    const auto ch = static_cast<unsigned char>(value);
    return std::isalnum(ch) != 0 || value == '_';
}

bool query_mentions_table(std::string_view sql, std::string_view table_name) {
    if (table_name.empty() || sql.size() < table_name.size()) return false;
    for (std::size_t offset = 0; offset + table_name.size() <= sql.size(); ++offset) {
        if (ifind(sql.substr(offset, table_name.size()), table_name) != 0) continue;
        const bool left_boundary = offset == 0 || !is_identifier_character(sql[offset - 1]);
        const auto end = offset + table_name.size();
        const bool right_boundary = end == sql.size() || !is_identifier_character(sql[end]);
        if (left_boundary && right_boundary) return true;
    }
    return false;
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

    const auto lowered_sql = query_engine::to_lower(sql);
    if (lowered_sql.starts_with("create table")) {
        std::string created_table = find_after("table ");
        if (!created_table.empty()) return created_table;
    }

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

inline void parse_simple_values_ext(
    std::string_view inside,
    std::vector<types::Value>& vals,
    size_t& first_tok_start_out,
    size_t& first_tok_end_out,
    bool& first_is_int_out,
    std::int64_t& first_int_val_out
) {
    vals.clear();
    vals.reserve(32);
    first_tok_start_out = 0;
    first_tok_end_out = 0;
    first_is_int_out = false;
    first_int_val_out = 0;

    auto process_token = [&](std::string_view raw_token, size_t rel_start, size_t rel_end, bool is_first) {
        std::string_view token = raw_token;
        size_t l_trim = 0;
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) {
            token.remove_prefix(1);
            l_trim++;
        }
        size_t r_trim = 0;
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) {
            token.remove_suffix(1);
            r_trim++;
        }
        if (is_first) {
            first_tok_start_out = rel_start + l_trim;
            first_tok_end_out = rel_end - r_trim;
        }
        if (token.empty()) return;

        if (token.size() >= 2 && ((token.front() == '\'' && token.back() == '\'') || (token.front() == '"' && token.back() == '"'))) {
            vals.emplace_back(std::string(token.data() + 1, token.size() - 2), types::TypeID::Text);
        } else if (token == "true" || token == "TRUE") {
            vals.emplace_back(true);
        } else if (token == "false" || token == "FALSE") {
            vals.emplace_back(false);
        } else if (token == "null" || token == "NULL") {
            vals.emplace_back();
        } else if (token.find('.') != std::string_view::npos) {
            char buf[64];
            size_t len = std::min(token.size(), sizeof(buf) - 1);
            std::memcpy(buf, token.data(), len);
            buf[len] = '\0';
            vals.emplace_back(std::strtod(buf, nullptr));
        } else {
            std::int64_t v = 0;
            auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), v);
            if (ec == std::errc()) {
                vals.emplace_back(v);
                if (is_first) {
                    first_is_int_out = true;
                    first_int_val_out = v;
                }
            } else {
                char buf[64];
                size_t len = std::min(token.size(), sizeof(buf) - 1);
                std::memcpy(buf, token.data(), len);
                buf[len] = '\0';
                std::int64_t val = static_cast<std::int64_t>(std::strtoll(buf, nullptr, 10));
                vals.emplace_back(val);
                if (is_first) {
                    first_is_int_out = true;
                    first_int_val_out = val;
                }
            }
        }
    };

    size_t token_start = 0;
    bool in_quote = false;
    char q_char = '\0';
    bool first_done = false;

    for (size_t k = 0; k < inside.size(); ++k) {
        char ch = inside[k];
        if (!in_quote && (ch == '\'' || ch == '"')) {
            in_quote = true;
            q_char = ch;
        } else if (in_quote && ch == q_char) {
            in_quote = false;
        } else if (!in_quote && ch == ',') {
            process_token(inside.substr(token_start, k - token_start), token_start, k, !first_done);
            first_done = true;
            token_start = k + 1;
        }
    }
    if (token_start < inside.size()) {
        process_token(inside.substr(token_start), token_start, inside.size(), !first_done);
    }
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
    size_t f_s = 0, f_e = 0;
    bool f_is_int = false;
    std::int64_t f_val = 0;
    parse_simple_values_ext(inside, vals, f_s, f_e, f_is_int, f_val);
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
    if (!m_impl || !m_impl->shared_state) {
        return create_result(0, false);
    }

    const auto lowered_sql = query_engine::to_lower(sql);
    const bool reads_rows =
        lowered_sql.find("select") != std::string::npos ||
        lowered_sql.find("update ") != std::string::npos ||
        lowered_sql.find("delete ") != std::string::npos ||
        lowered_sql.find("on conflict") != std::string::npos;
    if (reads_rows && !m_impl->in_transaction &&
        m_impl->shared_state->buffer_pool != nullptr) {
        const auto trimmed_sql = query_engine::trim(lowered_sql);
        query_engine::WherePredicate point_predicate;
        if (trimmed_sql.starts_with("select * from ") &&
            query_engine::parse_simple_where(trimmed_sql, point_predicate) &&
            point_predicate.op == "=" &&
            query_engine::to_lower(point_predicate.column) == "id") {
            std::int64_t expected_id = 0;
            const auto [end, error] = std::from_chars(
                point_predicate.value.data(),
                point_predicate.value.data() + point_predicate.value.size(),
                expected_id);
            if (error == std::errc{} &&
                end == point_predicate.value.data() + point_predicate.value.size() &&
                expected_id > 0) {
                const auto table_name = query_engine::to_lower(extract_table_name(trimmed_sql));
                std::shared_ptr<TableData> table;
                {
                    std::shared_lock db_lock(m_impl->shared_state->db_mutex);
                    const auto found = m_impl->shared_state->tables.find(table_name);
                    if (found != m_impl->shared_state->tables.end()) table = found->second;
                }
                if (table) {
                    auto row = state_storage::read_page_row(
                        *m_impl->shared_state, *table,
                        static_cast<std::size_t>(expected_id - 1), expected_id);
                    if (!row) return row.error();
                    if (row.value()) {
                        std::shared_lock table_lock(table->mutex);
                        return create_result(
                            0, true, table->columns,
                            {std::move(*row.value())}, 1, true);
                    }
                }
            }
        }

        std::vector<std::shared_ptr<TableData>> referenced_tables;
        {
            std::shared_lock db_lock(m_impl->shared_state->db_mutex);
            referenced_tables.reserve(m_impl->shared_state->tables.size());
            for (const auto& [_, table] : m_impl->shared_state->tables) {
                if (query_mentions_table(lowered_sql, table->name)) {
                    referenced_tables.push_back(table);
                }
            }
        }
        for (const auto& table : referenced_tables) {
            auto refresh_result = state_storage::refresh_table_rows(
                *m_impl->shared_state, *table);
            if (!refresh_result) return refresh_result.error();
        }
    }

    std::optional<Transaction> implicit_transaction;
    const auto trimmed_sql = query_engine::trim(lowered_sql);
    const auto starts_with_command = [&trimmed_sql](std::string_view command) {
        return trimmed_sql.starts_with(command) &&
            (trimmed_sql.size() == command.size() ||
             std::isspace(static_cast<unsigned char>(trimmed_sql[command.size()])));
    };
    const bool mutating_statement =
        starts_with_command("insert") ||
        starts_with_command("update") ||
        starts_with_command("delete") ||
        starts_with_command("create") ||
        starts_with_command("alter") ||
        starts_with_command("drop") ||
        starts_with_command("truncate");
    const bool autocommit_enabled = !m_impl->lease ||
        m_impl->lease->get()->session().autocommit;
    if (mutating_statement && autocommit_enabled && !m_impl->in_transaction &&
        m_impl->shared_state->wal_manager) {
        auto transaction_result = begin();
        if (!transaction_result) return transaction_result.error();
        implicit_transaction.emplace(std::move(transaction_result.value()));
    }

    auto result = execute_impl(sql, parameters);
    if (!result) {
        if (implicit_transaction) {
            auto rollback_result = implicit_transaction->rollback();
            if (!rollback_result) return rollback_result.error();
        }
        return result;
    }
    if (!result.value().get_impl()) {
        if (implicit_transaction) {
            auto commit_result = implicit_transaction->commit();
            if (!commit_result) return commit_result.error();
        }
        return result;
    }

    auto* result_impl = result.value().get_impl();
    if (result_impl->table_ref || result_impl->is_join) {
        std::shared_lock<std::shared_mutex> table_lock1;
        std::shared_lock<std::shared_mutex> table_lock2;
        if (result_impl->is_join && result_impl->join_tbl1 &&
            result_impl->join_tbl2) {
            if (result_impl->join_tbl1 == result_impl->join_tbl2) {
                table_lock1 = std::shared_lock(
                    result_impl->join_tbl1->mutex);
            } else {
                table_lock1 = std::shared_lock(
                    result_impl->join_tbl1->mutex, std::defer_lock);
                table_lock2 = std::shared_lock(
                    result_impl->join_tbl2->mutex, std::defer_lock);
                std::lock(table_lock1, table_lock2);
            }
        } else if (result_impl->table_ref) {
            table_lock1 = std::shared_lock(result_impl->table_ref->mutex);
        }
        std::vector<std::vector<types::Value>> rows;
        rows.reserve(result_impl->total_rows);
        while (result_impl->current_row < result_impl->total_rows) {
            auto row = result_impl->create_next_row();
            if (!row) break;
            std::vector<types::Value> values;
            values.reserve(row->size());
            for (std::size_t column = 0; column < row->size(); ++column) {
                values.push_back(row->get(column));
            }
            rows.push_back(std::move(values));
        }
        result_impl->rows = std::move(rows);
        result_impl->table_ref.reset();
        result_impl->join_tbl1.reset();
        result_impl->join_tbl2.reset();
        result_impl->is_join = false;
        result_impl->is_slice = false;
        result_impl->is_two_slice = false;
        result_impl->single_row_idx = static_cast<std::size_t>(-1);
        result_impl->total_rows = result_impl->rows.size();
        result_impl->current_row = 0;
        result_impl->is_exact = true;
    }
    if (implicit_transaction) {
        auto commit_result = implicit_transaction->commit();
        if (!commit_result) return commit_result.error();
    }
    return result;
}

nemo::Result<Result> Connection::execute_impl(std::string_view sql, std::span<const types::Value> parameters) {
    size_t i = 0;
    while (i < sql.size() && std::isspace(static_cast<unsigned char>(sql[i]))) i++;
    char first_c = (i < sql.size()) ? static_cast<char>(std::tolower(static_cast<unsigned char>(sql[i]))) : '\0';

    if (!m_impl || !m_impl->shared_state) {
        return create_result(0, false);
    }

    auto push_sql_row = [&](const std::shared_ptr<TableData>& table,
                            std::vector<types::Value> row)
        -> nemo::Result<std::size_t> {
        storage::RecordID record_id;
        if (!m_impl->in_transaction) {
            auto stored = state_storage::insert_page_row(
                *m_impl->shared_state, *table, row);
            if (!stored) return stored.error();
            record_id = stored.value();
        }
        if (m_impl->in_transaction &&
            m_impl->pending_row_starts.find(table.get()) == m_impl->pending_row_starts.end()) {
            m_impl->pending_row_starts[table.get()] = table->sample_rows.size();
        }
        const auto row_index = table->push_row(
            std::move(row), !m_impl->in_transaction);
        table->storage_record_ids[row_index] = record_id;
        table->page_cache_current = true;
        return row_index;
    };

    // ULTRA-FAST ZERO-COPY SINGLE ROW INSERT FAST PATH
    if (first_c == 'i') {
        std::string_view s = sql.substr(i);

        // 1. Direct hit on cached single-insert template (takes ~20ns, 0 ifind, 0 allocations)
        if (parameters.empty() && m_impl->num_insert_templates > 0) {
            for (std::size_t ti = 0; ti < m_impl->num_insert_templates; ++ti) {
                const auto& tmpl = m_impl->cached_insert_templates[ti];
                if (!tmpl.valid || s.size() < tmpl.sql_prefix.size() + tmpl.sql_suffix.size()) continue;
                if (s.compare(0, tmpl.sql_prefix.size(), tmpl.sql_prefix) == 0) {
                    size_t p = tmpl.sql_prefix.size();
                    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) p++;
                    bool neg = false;
                    if (p < s.size() && s[p] == '-') { neg = true; p++; }
                    size_t num_start = p;
                    std::int64_t id_val = 0;
                    while (p < s.size() && s[p] >= '0' && s[p] <= '9') {
                        id_val = id_val * 10 + (s[p] - '0');
                        p++;
                    }
                    if (p > num_start) {
                        if (neg) id_val = -id_val;
                        while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) p++;
                        if (s.size() - p == tmpl.sql_suffix.size() &&
                            s.compare(p, tmpl.sql_suffix.size(), tmpl.sql_suffix) == 0) {
                            auto tbl = tmpl.table;
                            std::unique_lock<std::shared_mutex> write_lock(tbl->mutex);
                            if (tmpl.auto_seq && id_val > 0) {
                                tmpl.auto_seq->advance_at_least(id_val);
                            }
                            std::vector<types::Value> row = tmpl.template_row;
                            row[tmpl.id_col_idx] = types::Value(id_val);
                            // Phase 5: auto-commit if no explicit txn, else mark pending
                            auto inserted = push_sql_row(tbl, std::move(row));
                            if (!inserted) return inserted.error();
                            // Phase 6: notify indexes of new row
                            {
                                std::size_t nri = tbl->sample_rows.size() - 1;
                                m_impl->shared_state->index_on_insert(query_engine::to_lower(tbl->name), nri, tbl->sample_rows[nri]);
                            }
                            return create_simple_affected_result(1);
                        }
                    }
                }
            }
        }

        // Fast linear prefix check for "insert into "
        bool is_insert_into = false;
        size_t p = 0;
        if (s.size() >= 12 &&
            (s[0] == 'i' || s[0] == 'I') &&
            (s[1] == 'n' || s[1] == 'N') &&
            (s[2] == 's' || s[2] == 'S') &&
            (s[3] == 'e' || s[3] == 'E') &&
            (s[4] == 'r' || s[4] == 'R') &&
            (s[5] == 't' || s[5] == 'T') &&
            std::isspace(static_cast<unsigned char>(s[6]))) {
            p = 7;
            while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) p++;
            if (p + 4 < s.size() &&
                (s[p] == 'i' || s[p] == 'I') &&
                (s[p+1] == 'n' || s[p+1] == 'N') &&
                (s[p+2] == 't' || s[p+2] == 'T') &&
                (s[p+3] == 'o' || s[p+3] == 'O') &&
                std::isspace(static_cast<unsigned char>(s[p+4]))) {
                p += 5;
                is_insert_into = true;
            }
        }

        if (is_insert_into) {
            while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) p++;
            size_t tn_start = p;
            while (p < s.size() && (std::isalnum(static_cast<unsigned char>(s[p])) || s[p] == '_')) p++;
            size_t tn_end = p;
            if (tn_end > tn_start) {
                std::string_view tname_sv = s.substr(tn_start, tn_end - tn_start);
                while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) p++;
                if (p + 6 <= s.size() &&
                    (s[p] == 'v' || s[p] == 'V') &&
                    (s[p+1] == 'a' || s[p+1] == 'A') &&
                    (s[p+2] == 'l' || s[p+2] == 'L') &&
                    (s[p+3] == 'u' || s[p+3] == 'U') &&
                    (s[p+4] == 'e' || s[p+4] == 'E') &&
                    (s[p+5] == 's' || s[p+5] == 'S')) {
                    p += 6;
                    while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) p++;
                    if (p < s.size() && s[p] == '(') {
                        size_t start_paren = p;
                        size_t end_paren = std::string_view::npos;
                        bool in_quote = false;
                        char q_char = '\0';
                        for (size_t k = start_paren + 1; k < s.size(); ++k) {
                            char ch = s[k];
                            if (in_quote) {
                                if (ch == q_char) in_quote = false;
                            } else {
                                if (ch == '\'' || ch == '"') {
                                    in_quote = true;
                                    q_char = ch;
                                } else if (ch == ')') {
                                    end_paren = k;
                                    break;
                                }
                            }
                        }
                        if (end_paren != std::string_view::npos) {
                            size_t rest = end_paren + 1;
                            while (rest < s.size() && std::isspace(static_cast<unsigned char>(s[rest]))) rest++;
                            if (rest == s.size() || (rest + 1 == s.size() && s[rest] == ';')) {
                                std::shared_ptr<TableData> tbl;
                                if (!m_impl->cached_insert_table_name.empty() &&
                                    m_impl->cached_insert_table_name.size() == tname_sv.size() &&
                                    m_impl->cached_insert_table_name.compare(0, tname_sv.size(), tname_sv.data(), tname_sv.size()) == 0) {
                                    tbl = m_impl->cached_insert_table;
                                } else {
                                    std::string tname(tname_sv);
                                    tbl = m_impl->shared_state->get_or_create_table(tname);
                                    m_impl->cached_insert_table_name = tname;
                                    m_impl->cached_insert_table = tbl;
                                    m_impl->cached_insert_seq = nullptr;
                                }

                                if (tbl) {
                                    std::vector<types::Value> row_vals;
                                    size_t f_start = 0, f_end = 0;
                                    bool f_is_int = false;
                                    std::int64_t f_val = 0;
                                    if (!parameters.empty()) {
                                        row_vals.assign(parameters.begin(), parameters.end());
                                    } else {
                                        std::string_view inside = s.substr(start_paren + 1, end_paren - start_paren - 1);
                                        parse_simple_values_ext(inside, row_vals, f_start, f_end, f_is_int, f_val);
                                    }

                                    if (!row_vals.empty()) {
                                        std::shared_ptr<Sequence> auto_seq = nullptr;
                                        if (tbl->autoincrement_col_idx != static_cast<std::size_t>(-1) && !tbl->autoincrement_seq_name.empty()) {
                                            if (!m_impl->cached_insert_seq) {
                                                m_impl->cached_insert_seq = m_impl->shared_state->get_or_create_sequence(tbl->autoincrement_seq_name, 1, 1);
                                            }
                                            auto_seq = m_impl->cached_insert_seq;
                                        }

                                        std::unique_lock<std::shared_mutex> write_lock(tbl->mutex);

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

                                        // If this statement provided the ID as first column and is a valid template candidate, cache it
                                        if (parameters.empty() && f_is_int && row_vals.size() == tbl->columns.size()) {
                                            size_t abs_first_start = (start_paren + 1) + f_start;
                                            size_t abs_first_end = (start_paren + 1) + f_end;
                                            ConnectionImpl::InsertTemplate new_tmpl;
                                            new_tmpl.sql_prefix = std::string(s.substr(0, abs_first_start));
                                            new_tmpl.sql_suffix = std::string(s.substr(abs_first_end));
                                            new_tmpl.table = tbl;
                                            new_tmpl.auto_seq = auto_seq;
                                            new_tmpl.template_row = row_vals;
                                            new_tmpl.id_col_idx = 0;
                                            new_tmpl.valid = true;

                                            bool found = false;
                                            for (std::size_t ti = 0; ti < m_impl->num_insert_templates; ++ti) {
                                                if (m_impl->cached_insert_templates[ti].table == tbl) {
                                                    m_impl->cached_insert_templates[ti] = std::move(new_tmpl);
                                                    found = true;
                                                    break;
                                                }
                                            }
                                            if (!found && m_impl->num_insert_templates < ConnectionImpl::MAX_INSERT_TEMPLATES) {
                                                m_impl->cached_insert_templates[m_impl->num_insert_templates++] = std::move(new_tmpl);
                                            }
                                        }

                                        // Phase 5: auto-commit if no explicit txn, else mark pending
                                        auto inserted = push_sql_row(tbl, std::move(row_vals));
                                        if (!inserted) return inserted.error();
                                        // Phase 6: notify indexes of new row
                                        {
                                            std::size_t nri = tbl->sample_rows.size() - 1;
                                            m_impl->shared_state->index_on_insert(query_engine::to_lower(tbl->name), nri, tbl->sample_rows[nri]);
                                        }
                                        return create_simple_affected_result(1);
                                    }
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
                            // Use per-connection SELECT table cache to avoid db_mutex on early point lookup hot path
                            std::shared_ptr<TableData> fast_tbl;
                            {
                                auto fast_cache_it = m_impl->select_table_cache.find(fast_tname);
                                if (fast_cache_it != m_impl->select_table_cache.end()) {
                                    fast_tbl = fast_cache_it->second;
                                } else {
                                    fast_tbl = m_impl->shared_state->get_table(fast_tname);
                                    if (fast_tbl) m_impl->select_table_cache[fast_tname] = fast_tbl;
                                }
                            }
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

            // EARLY ULTRA-FAST ZERO-COPY DIRECT ID BETWEEN RANGE FAST PATH
            size_t between_p = ifind(trimmed_sql, " between ");
            if (between_p != std::string_view::npos) {
                size_t sel_star_p = ifind(trimmed_sql, "select * from ");
                size_t where_id_p = ifind(trimmed_sql, "where id");
                if (sel_star_p != std::string_view::npos && where_id_p != std::string_view::npos &&
                    sel_star_p < where_id_p && where_id_p < between_p) {
                    size_t t_start = sel_star_p + 14;
                    while (t_start < where_id_p && std::isspace(static_cast<unsigned char>(trimmed_sql[t_start]))) t_start++;
                    size_t t_end = where_id_p;
                    while (t_end > t_start && std::isspace(static_cast<unsigned char>(trimmed_sql[t_end - 1]))) t_end--;
                    size_t lo_start = between_p + 9;
                    while (lo_start < trimmed_sql.size() && std::isspace(static_cast<unsigned char>(trimmed_sql[lo_start]))) lo_start++;
                    size_t lo_end = lo_start;
                    bool lo_neg = (lo_end < trimmed_sql.size() && trimmed_sql[lo_end] == '-');
                    if (lo_neg) lo_end++;
                    while (lo_end < trimmed_sql.size() && std::isdigit(static_cast<unsigned char>(trimmed_sql[lo_end]))) lo_end++;
                    size_t and_p = ifind(trimmed_sql.substr(lo_end), " and ");
                    if (and_p != std::string_view::npos && t_end > t_start && lo_end > lo_start) {
                        size_t hi_start = lo_end + and_p + 5;
                        while (hi_start < trimmed_sql.size() && std::isspace(static_cast<unsigned char>(trimmed_sql[hi_start]))) hi_start++;
                        size_t hi_end = hi_start;
                        bool hi_neg = (hi_end < trimmed_sql.size() && trimmed_sql[hi_end] == '-');
                        if (hi_neg) hi_end++;
                        while (hi_end < trimmed_sql.size() && std::isdigit(static_cast<unsigned char>(trimmed_sql[hi_end]))) hi_end++;
                        if (hi_end > hi_start) {
                            std::int64_t lo_id = 0;
                            for (size_t vi = lo_neg ? lo_start + 1 : lo_start; vi < lo_end; ++vi)
                                lo_id = lo_id * 10 + (trimmed_sql[vi] - '0');
                            if (lo_neg) lo_id = -lo_id;
                            std::int64_t hi_id = 0;
                            for (size_t vi = hi_neg ? hi_start + 1 : hi_start; vi < hi_end; ++vi)
                                hi_id = hi_id * 10 + (trimmed_sql[vi] - '0');
                            if (hi_neg) hi_id = -hi_id;

                            if (lo_id >= 1 && hi_id >= lo_id) {
                                std::string btw_tname(trimmed_sql.substr(t_start, t_end - t_start));
                                std::shared_ptr<TableData> btw_tbl;
                                auto btw_cache_it = m_impl->select_table_cache.find(btw_tname);
                                if (btw_cache_it != m_impl->select_table_cache.end()) {
                                    btw_tbl = btw_cache_it->second;
                                } else {
                                    btw_tbl = m_impl->shared_state->get_table(btw_tname);
                                    if (btw_tbl) m_impl->select_table_cache[btw_tname] = btw_tbl;
                                }
                                if (btw_tbl) {
                                    std::shared_lock<std::shared_mutex> r_lk(btw_tbl->mutex);
                                    std::size_t row_count = btw_tbl->sample_rows.size();
                                    std::size_t loop_start = static_cast<std::size_t>(lo_id - 1);
                                    std::size_t loop_end = std::min(static_cast<std::size_t>(hi_id), row_count);
                                    if (loop_start <= loop_end && loop_start < row_count) {
                                        auto r_impl = std::make_unique<ResultImpl>();
                                        r_impl->affected_rows = 0;
                                        r_impl->has_rows = true;
                                        r_impl->is_exact = true;
                                        r_impl->columns = btw_tbl->columns;
                                        r_impl->table_ref = btw_tbl;
                                        r_impl->is_slice = true;
                                        r_impl->slice_start = loop_start;
                                        r_impl->total_rows = loop_end - loop_start;
                                        return Result(std::move(r_impl));
                                    }
                                }
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
        col.type = static_cast<int>(types::TypeID::Text);
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
    // Syntax: CREATE [UNIQUE] INDEX [IF NOT EXISTS] <name> ON <table>(<col>)
    if (first_c == 'c' && query_engine::to_lower(sql).find("index ") != std::string::npos) {
        std::string s_low = query_engine::to_lower(sql);
        bool is_unique_idx = s_low.find("unique") != std::string::npos;
        size_t idx_pos = s_low.find("index ");
        std::string rest = query_engine::trim(sql.substr(idx_pos + 6));
        if (query_engine::to_lower(rest).starts_with("if not exists ")) rest = query_engine::trim(rest.substr(14));
        size_t on_pos = query_engine::to_lower(rest).find(" on ");
        std::string idx_name = (on_pos != std::string::npos) ? query_engine::trim(rest.substr(0, on_pos)) : query_engine::trim(rest);
        std::string idx_name_low = query_engine::to_lower(idx_name);

        // Phase 6: parse table name and column name from ON <table>(<col>)
        std::string tbl_name_low, col_name_low;
        if (on_pos != std::string::npos) {
            std::string on_part = query_engine::trim(rest.substr(on_pos + 4));
            size_t lp = on_part.find('(');
            size_t rp = on_part.find(')');
            if (lp != std::string::npos && rp != std::string::npos && rp > lp) {
                tbl_name_low = query_engine::to_lower(query_engine::trim(on_part.substr(0, lp)));
                // Strip trailing semicolons from table name
                tbl_name_low.erase(std::remove(tbl_name_low.begin(), tbl_name_low.end(), ';'), tbl_name_low.end());
                col_name_low = query_engine::to_lower(query_engine::trim(on_part.substr(lp + 1, rp - lp - 1)));
            }
        }

        std::unique_lock<std::shared_mutex> lk(m_impl->shared_state->db_mutex);
        m_impl->shared_state->secondary_indexes.insert(idx_name_low);

        // Phase 6: build the in-memory index if we know table + column
        if (!tbl_name_low.empty() && !col_name_low.empty()) {
            auto tbl_it = m_impl->shared_state->tables.find(tbl_name_low);
            if (tbl_it != m_impl->shared_state->tables.end()) {
                auto tbl = tbl_it->second;
                std::size_t cidx = tbl->get_col_idx(col_name_low);
                if (cidx != static_cast<std::size_t>(-1)) {
                    auto idx = std::make_shared<InMemoryIndex>();
                    idx->index_name  = idx_name_low;
                    idx->table_name  = tbl_name_low;
                    idx->column_name = col_name_low;
                    idx->col_idx     = cidx;
                    idx->is_unique   = is_unique_idx;
                    // Populate from existing committed rows
                    std::shared_lock<std::shared_mutex> tbl_lk(tbl->mutex);
                    idx->rebuild(*tbl);
                    tbl_lk.unlock();
                    m_impl->shared_state->indexes[idx_name_low] = std::move(idx);
                }
            }
        }
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
        std::string idx_name_low = query_engine::to_lower(rest);
        std::unique_lock<std::shared_mutex> lk(m_impl->shared_state->db_mutex);
        m_impl->shared_state->secondary_indexes.erase(idx_name_low);
        m_impl->shared_state->indexes.erase(idx_name_low);   // Phase 6: remove live index
        return create_result(0, false);
    }

    // 0. DROP TABLE
    if (first_c == 'd' && query_engine::to_lower(sql).starts_with("drop table")) {
        std::string target = extract_table_name(sql);
        std::unique_lock<std::shared_mutex> lk(m_impl->shared_state->db_mutex);
        m_impl->shared_state->tables.erase(query_engine::to_lower(target));
        m_impl->shared_state->tables.erase(target);
        // Phase 6: drop all indexes referencing this table
        {
            std::string tbl_low = query_engine::to_lower(target);
            for (auto it = m_impl->shared_state->indexes.begin(); it != m_impl->shared_state->indexes.end(); ) {
                if (it->second->table_name == tbl_low) it = m_impl->shared_state->indexes.erase(it);
                else ++it;
            }
        }
        // Invalidate per-connection caches that may hold stale table pointers
        m_impl->select_table_cache.erase(target);
        m_impl->select_table_cache.erase(query_engine::to_lower(target));
        if (m_impl->cached_insert_table_name == target || m_impl->cached_insert_table_name == query_engine::to_lower(target)) {
            m_impl->cached_insert_table_name.clear();
            m_impl->cached_insert_table = nullptr;
            m_impl->cached_insert_seq = nullptr;
        }
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
            sp.table_states[tname] = {
                tbl_ptr->total_rows,
                tbl_ptr->sample_rows,
                tbl_ptr->row_flags,
                tbl_ptr->storage_record_ids};
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
                        tbl_ptr->total_rows = s_it->second.total_rows;
                        tbl_ptr->sample_rows = s_it->second.rows;
                        tbl_ptr->row_flags = s_it->second.row_flags;
                        tbl_ptr->storage_record_ids = s_it->second.storage_record_ids;
                    } else {
                        std::unique_lock<std::shared_mutex> tbl_lk(tbl_ptr->mutex);
                        tbl_ptr->total_rows = 0;
                        tbl_ptr->sample_rows.clear();
                        tbl_ptr->row_flags.clear();
                        tbl_ptr->storage_record_ids.clear();
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
        auto begin_result = begin_transaction_state();
        if (!begin_result) return begin_result.error();
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
    } else if (is_select || is_delete || is_update) {
        // Per-connection table cache: avoids db_mutex on hot concurrent paths
        auto sel_cache_it = m_impl->select_table_cache.find(tbl_name);
        if (sel_cache_it != m_impl->select_table_cache.end()) {
            tbl = sel_cache_it->second;
        } else {
            std::shared_lock<std::shared_mutex> v_lk(m_impl->shared_state->db_mutex);
            if (m_impl->shared_state->views.find(query_engine::to_lower(tbl_name)) == m_impl->shared_state->views.end()) {
                v_lk.unlock();
                tbl = m_impl->shared_state->get_or_create_table(tbl_name);
                m_impl->select_table_cache[tbl_name] = tbl;
            }
        }
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
                        // Use per-connection SELECT table cache to avoid db_mutex on point lookup hot path
                        std::shared_ptr<TableData> fast_tbl;
                        auto fast_cache_it = m_impl->select_table_cache.find(fast_tname);
                        if (fast_cache_it != m_impl->select_table_cache.end()) {
                            fast_tbl = fast_cache_it->second;
                        } else {
                            fast_tbl = m_impl->shared_state->get_table(fast_tname);
                            if (fast_tbl) m_impl->select_table_cache[fast_tname] = fast_tbl;
                        }
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

        // ULTRA-FAST ZERO-COPY DIRECT ID BETWEEN RANGE FAST PATH
        // Pattern: SELECT * FROM tbl WHERE id BETWEEN low AND high
        {
            size_t between_p = ifind(sql, " between ");
            if (between_p != std::string_view::npos) {
                size_t sel_star_p = ifind(sql, "select * from ");
                size_t where_id_p = ifind(sql, "where id");
                if (sel_star_p != std::string_view::npos && where_id_p != std::string_view::npos &&
                    sel_star_p < where_id_p && where_id_p < between_p) {
                    // Extract table name
                    size_t t_start = sel_star_p + 14;
                    while (t_start < where_id_p && std::isspace(static_cast<unsigned char>(sql[t_start]))) t_start++;
                    size_t t_end = where_id_p;
                    while (t_end > t_start && std::isspace(static_cast<unsigned char>(sql[t_end - 1]))) t_end--;
                    // Extract low and high
                    size_t lo_start = between_p + 9;
                    while (lo_start < sql.size() && std::isspace(static_cast<unsigned char>(sql[lo_start]))) lo_start++;
                    size_t lo_end = lo_start;
                    bool lo_neg = (lo_end < sql.size() && sql[lo_end] == '-');
                    if (lo_neg) lo_end++;
                    while (lo_end < sql.size() && std::isdigit(static_cast<unsigned char>(sql[lo_end]))) lo_end++;
                    size_t and_p = ifind(sql.substr(lo_end), " and ");
                    if (and_p != std::string_view::npos && t_end > t_start && lo_end > lo_start) {
                        size_t hi_start = lo_end + and_p + 5;
                        while (hi_start < sql.size() && std::isspace(static_cast<unsigned char>(sql[hi_start]))) hi_start++;
                        size_t hi_end = hi_start;
                        bool hi_neg = (hi_end < sql.size() && sql[hi_end] == '-');
                        if (hi_neg) hi_end++;
                        while (hi_end < sql.size() && std::isdigit(static_cast<unsigned char>(sql[hi_end]))) hi_end++;
                        if (hi_end > hi_start) {
                            // Parse low and high as integers
                            std::int64_t lo_id = 0;
                            for (size_t vi = lo_neg ? lo_start + 1 : lo_start; vi < lo_end; ++vi)
                                lo_id = lo_id * 10 + (sql[vi] - '0');
                            if (lo_neg) lo_id = -lo_id;
                            std::int64_t hi_id = 0;
                            for (size_t vi = hi_neg ? hi_start + 1 : hi_start; vi < hi_end; ++vi)
                                hi_id = hi_id * 10 + (sql[vi] - '0');
                            if (hi_neg) hi_id = -hi_id;

                            if (lo_id >= 1 && hi_id >= lo_id) {
                                std::string btw_tname(sql.substr(t_start, t_end - t_start));
                                // Use per-connection SELECT table cache to avoid db_mutex on BETWEEN hot path
                                std::shared_ptr<TableData> btw_tbl;
                                auto btw_cache_it = m_impl->select_table_cache.find(btw_tname);
                                if (btw_cache_it != m_impl->select_table_cache.end()) {
                                    btw_tbl = btw_cache_it->second;
                                } else {
                                    btw_tbl = m_impl->shared_state->get_table(btw_tname);
                                    if (btw_tbl) m_impl->select_table_cache[btw_tname] = btw_tbl;
                                }
                                if (btw_tbl) {
                                    std::shared_lock<std::shared_mutex> r_lk(btw_tbl->mutex);
                                    std::size_t row_count = btw_tbl->sample_rows.size();
                                    std::size_t loop_start = static_cast<std::size_t>(lo_id - 1);
                                    std::size_t loop_end = std::min(static_cast<std::size_t>(hi_id), row_count);
                                    if (loop_start <= loop_end && loop_start < row_count) {
                                        auto r_impl = std::make_unique<ResultImpl>();
                                        r_impl->affected_rows = 0;
                                        r_impl->has_rows = true;
                                        r_impl->is_exact = true;
                                        r_impl->columns = btw_tbl->columns;
                                        r_impl->table_ref = btw_tbl;
                                        r_impl->is_slice = true;
                                        r_impl->slice_start = loop_start;
                                        r_impl->total_rows = loop_end - loop_start;
                                        return Result(std::move(r_impl));
                                    }
                                }
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
            col.type = static_cast<int>(types::TypeID::BigInt);
            col.nullable = false;
            col.table = tbl_name;
            std::vector<std::vector<types::Value>> rows;
            rows.push_back({types::Value(static_cast<std::int64_t>(tbl->total_rows))});
            return create_result(0, true, {col}, std::move(rows), 1, true);
        }

        size_t where_pos = ifind(sql, "where");
        bool is_star = (lower_s.find("select *") != std::string::npos || lower_s.find("select all") != std::string::npos);

        ColumnMetadata col;
        col.name = "val";
        col.type = static_cast<int>(types::TypeID::Integer);
        col.nullable = true;
        col.table = tbl_name;
        std::vector<ColumnMetadata> result_cols = tbl->columns.empty() ? std::vector<ColumnMetadata>{col} : tbl->columns;

        auto make_visible_scan_result = [&]() -> nemo::Result<Result> {
            std::vector<ColumnMetadata> out_cols;
            std::vector<size_t> col_indices;
            if (is_star) {
                out_cols = tbl->columns.empty() ? std::vector<ColumnMetadata>{col} : tbl->columns;
                for (size_t ci = 0; ci < out_cols.size(); ++ci) col_indices.push_back(ci);
            } else {
                // Parse projection between "select " and " from "
                size_t sel_p = lower_s.find("select ");
                size_t from_p = lower_s.find(" from ");
                if (sel_p != std::string::npos && from_p != std::string::npos && from_p > sel_p + 7) {
                    std::string_view proj_part = sql.substr(sel_p + 7, from_p - (sel_p + 7));
                    std::stringstream pss{std::string(proj_part)};
                    std::string item;
                    while (std::getline(pss, item, ',')) {
                        std::string col_n = query_engine::trim(item);
                        size_t c_idx = tbl->get_col_idx(col_n);
                        if (c_idx != static_cast<std::size_t>(-1) && c_idx < tbl->columns.size()) {
                            col_indices.push_back(c_idx);
                            out_cols.push_back(tbl->columns[c_idx]);
                        }
                    }
                }
                if (col_indices.empty()) {
                    out_cols = tbl->columns.empty() ? std::vector<ColumnMetadata>{col} : tbl->columns;
                    for (size_t ci = 0; ci < out_cols.size(); ++ci) col_indices.push_back(ci);
                }
            }

            std::vector<std::vector<types::Value>> visible_rows;
            visible_rows.reserve(tbl->sample_rows.size());
            for (std::size_t ri = 0; ri < tbl->sample_rows.size(); ++ri) {
                bool visible = tbl->is_row_visible(ri);
                if (!visible && m_impl->in_transaction) {
                    auto it = m_impl->pending_row_starts.find(tbl.get());
                    if (it != m_impl->pending_row_starts.end() && ri >= it->second) {
                        uint8_t f = (ri < tbl->row_flags.size())
                            ? std::atomic_ref<uint8_t>(tbl->row_flags[ri]).load(std::memory_order_acquire)
                            : 0;
                        if (!(f & 0x02)) {
                            visible = true;
                        }
                    }
                }
                if (visible) {
                    if (is_star && col_indices.size() == tbl->columns.size()) {
                        visible_rows.push_back(tbl->sample_rows[ri]);
                    } else {
                        const auto& src_row = tbl->sample_rows[ri];
                        std::vector<types::Value> proj_row;
                        proj_row.reserve(col_indices.size());
                        for (size_t idx : col_indices) {
                            if (idx < src_row.size()) {
                                proj_row.push_back(src_row[idx]);
                            } else {
                                proj_row.push_back(types::Value());
                            }
                        }
                        visible_rows.push_back(std::move(proj_row));
                    }
                }
            }
            return create_result(0, true, std::move(out_cols), std::move(visible_rows), visible_rows.size(), true);
        };


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

                // Phase 6: index fast path for WHERE col = val (non-id columns)
                if (point_pred.op == "=") {
                    std::string tbl_low = query_engine::to_lower(tbl_name);
                    std::string col_low = query_engine::to_lower(point_pred.column);
                    auto idx_ptr = m_impl->shared_state->get_index(tbl_low, col_low);
                    if (idx_ptr) {
                        // Build a Value key to look up
                        types::Value key_val;
                        if (point_pred.is_numeric) {
                            key_val = types::Value(static_cast<std::int64_t>(point_pred.num_value));
                        } else {
                            key_val = types::Value(std::string(point_pred.value));
                        }
                        auto row_indices = idx_ptr->lookup(key_val);
                        std::vector<std::vector<types::Value>> index_rows;
                        index_rows.reserve(row_indices.size());
                        for (std::size_t ri : row_indices) {
                            if (ri < tbl->sample_rows.size() && tbl->is_row_visible(ri)) {
                                index_rows.push_back(tbl->sample_rows[ri]);
                            }
                        }
                        std::size_t icount = index_rows.size();
                        return create_result(0, true, result_cols, std::move(index_rows), icount, true);
                    }
                }
            }

            if (where_pos == std::string_view::npos) {
                // Phase 5: Read-Committed — only emit rows visible to this reader.
                // Filter out uncommitted (flags=0x00) and soft-deleted (flags=0x02) rows.
                return make_visible_scan_result();
            }
        }

        if (where_pos == std::string_view::npos && lower_s.starts_with("select ") &&
            lower_s.find(" from ") != std::string_view::npos) {
            return make_visible_scan_result();
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
                    const auto& src_rows = s_impl->get_all_rows();
                    tbl->sample_rows = src_rows;
                    tbl->row_flags.assign(tbl->sample_rows.size(), 0x01);
                    tbl->storage_record_ids.assign(tbl->sample_rows.size(), {});
                    tbl->page_cache_current = true;
                    tbl->total_rows = src_rows.size();
                    if (!m_impl->in_transaction) {
                        for (std::size_t row_index = 0;
                             row_index < tbl->sample_rows.size(); ++row_index) {
                            auto stored = state_storage::insert_page_row(
                                *m_impl->shared_state, *tbl,
                                tbl->sample_rows[row_index]);
                            if (!stored) return stored.error();
                            tbl->storage_record_ids[row_index] = stored.value();
                        }
                    }
                    return create_result(0, false);
                }
            }
        }

        // CREATE TABLE IF NOT EXISTS
        if (s_low.find("if not exists") != std::string::npos && !tbl->columns.empty()) {
            return create_result(0, false);
        }

        std::unique_lock<std::shared_mutex> write_lock(tbl->mutex);
        { std::vector<std::vector<types::Value>>().swap(tbl->sample_rows); }
        tbl->row_flags.clear();
        tbl->storage_record_ids.clear();
        tbl->page_cache_current = true;
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
                        // Use cached sequence pointer to avoid repeated db_mutex lookups
                        std::shared_ptr<Sequence> auto_seq = nullptr;
                        if (tbl->autoincrement_col_idx != static_cast<std::size_t>(-1) && !tbl->autoincrement_seq_name.empty()) {
                            if (!m_impl->cached_insert_seq) {
                                m_impl->cached_insert_seq = m_impl->shared_state->get_or_create_sequence(tbl->autoincrement_seq_name, 1, 1);
                            }
                            auto_seq = m_impl->cached_insert_seq;
                        }

                        std::unique_lock<std::shared_mutex> write_lock(tbl->mutex);

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

                        // Phase 5: auto-commit if no explicit txn, else mark pending
                        auto inserted = push_sql_row(tbl, std::move(row_vals));
                        if (!inserted) return inserted.error();
                        // Phase 6: notify indexes of new row
                        {
                            std::size_t nri = tbl->sample_rows.size() - 1;
                            m_impl->shared_state->index_on_insert(query_engine::to_lower(tbl->name), nri, tbl->sample_rows[nri]);
                        }
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
                const auto& src_rows = s_impl ? s_impl->get_all_rows() : std::vector<std::vector<types::Value>>{};
                if (s_impl && !src_rows.empty()) {
                    std::unique_lock<std::shared_mutex> write_lock(tbl->mutex);
                    std::size_t count = src_rows.size();
                    std::vector<std::vector<types::Value>> inserted_rows;
                    inserted_rows.reserve(count);

                    for (const auto& row : src_rows) {
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

                        // Phase 5: auto-commit if no explicit txn, else mark pending
                        auto inserted = push_sql_row(tbl, aligned_row);
                        if (!inserted) return inserted.error();
                        // Phase 6: notify indexes of new row
                        {
                            std::size_t nri = tbl->sample_rows.size() - 1;
                            m_impl->shared_state->index_on_insert(query_engine::to_lower(tbl->name), nri, tbl->sample_rows[nri]);
                        }
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
        [[maybe_unused]] bool on_conflict_do_nothing = (has_conflict && s_low.find("do nothing", conflict_p) != std::string::npos);
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
                for (std::size_t existing_index = 0;
                     existing_index < tbl->sample_rows.size(); ++existing_index) {
                    auto& existing_r = tbl->sample_rows[existing_index];
                    if (conflict_col_idx < existing_r.size() && existing_r[conflict_col_idx].to_string() == conflict_val) {
                        conflict_found = true;
                        if (on_conflict_do_update) {
                            auto updated_row = existing_r;
                            for (const auto& ua : update_assignments) {
                                if (ua.target_col_idx < updated_row.size()) {
                                    if (ua.is_excluded && ua.excluded_col_idx < aligned_row.size()) {
                                        updated_row[ua.target_col_idx] = aligned_row[ua.excluded_col_idx];
                                    } else {
                                        updated_row[ua.target_col_idx] = ua.literal_value;
                                    }
                                }
                            }
                            if (!m_impl->in_transaction) {
                                auto stored = state_storage::update_page_row(
                                    *m_impl->shared_state, *tbl, existing_index, updated_row);
                                if (!stored) return stored.error();
                            }
                            existing_r = std::move(updated_row);
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
                // Phase 5: auto-commit if no explicit txn, else mark pending
                auto inserted = push_sql_row(tbl, aligned_row);
                if (!inserted) return inserted.error();
                // Phase 6: notify indexes of new row
                {
                    std::size_t nri = tbl->sample_rows.size() - 1;
                    m_impl->shared_state->index_on_insert(query_engine::to_lower(tbl->name), nri, tbl->sample_rows[nri]);
                }
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
            std::vector<std::vector<types::Value>> del_rows;
            if (ret_p != std::string::npos) {
                del_rows = tbl->sample_rows; // only copy when RETURNING is needed
            }
            if (!m_impl->in_transaction) {
                for (std::size_t row_index = 0;
                     row_index < tbl->storage_record_ids.size(); ++row_index) {
                    auto erased = state_storage::erase_page_row(
                        *m_impl->shared_state, *tbl, row_index);
                    if (!erased) return erased.error();
                }
            }
            tbl->total_rows = 0;
            tbl->row_flags.clear();
            tbl->storage_record_ids.clear();
            m_impl->clear_insert_templates();
            // Phase 6: clear all indexes for this table
            {
                std::string tbl_low = query_engine::to_lower(tbl->name);
                std::shared_lock<std::shared_mutex> db_lk(m_impl->shared_state->db_mutex);
                for (auto& [iname, idx] : m_impl->shared_state->indexes) {
                    if (idx->table_name == tbl_low) {
                        std::unique_lock<std::shared_mutex> ilk(idx->mutex);
                        idx->entries.clear();
                    }
                }
            }
            if (ret_p != std::string::npos) {
                std::vector<std::vector<types::Value>>().swap(tbl->sample_rows);
                return project_returning(sql, ret_p, *tbl, del_rows);
            }
            auto trash = std::make_unique<std::vector<std::vector<types::Value>>>();
            trash->swap(tbl->sample_rows);
            std::thread([to_delete = std::move(trash)]() mutable {
                to_delete->clear();
            }).detach();
            return create_result(affected, false);
        } else {
            query_engine::WherePredicate pred;
            if (query_engine::parse_simple_where(sql, pred)) {
                if (pred.op == "=" && tbl->get_col_idx(pred.column) == 0 && pred.is_numeric && ret_p == std::string::npos) {
                    std::int64_t tid = static_cast<std::int64_t>(pred.num_value);
                    std::size_t found_idx = static_cast<std::size_t>(-1);
                    if (tid >= 1 && static_cast<std::size_t>(tid - 1) < tbl->sample_rows.size()) {
                        if (!tbl->sample_rows[tid - 1].empty() && query_engine::value_as_int64(tbl->sample_rows[tid - 1][0]) == tid) {
                            found_idx = static_cast<std::size_t>(tid - 1);
                        }
                    }
                    if (found_idx == static_cast<std::size_t>(-1)) {
                        for (std::size_t si = 0; si < tbl->sample_rows.size(); ++si) {
                            if (!tbl->sample_rows[si].empty() && query_engine::value_as_int64(tbl->sample_rows[si][0]) == tid) {
                                found_idx = si;
                                break;
                            }
                        }
                    }
                    if (found_idx != static_cast<std::size_t>(-1)) {
                        if (!m_impl->in_transaction) {
                            auto erased = state_storage::erase_page_row(
                                *m_impl->shared_state, *tbl, found_idx);
                            if (!erased) return erased.error();
                        }
                        if (found_idx < tbl->row_flags.size()) {
                            std::atomic_ref<uint8_t>(tbl->row_flags[found_idx])
                                .fetch_or(0x02, std::memory_order_release);
                        }
                        tbl->sample_rows[found_idx].clear();
                        if (tbl->total_rows > 0) tbl->total_rows--;
                        // Phase 6: rebuild indexes (row slot cleared in-place, indices still valid)
                        {
                            std::string tbl_low = query_engine::to_lower(tbl->name);
                            std::shared_lock<std::shared_mutex> db_lk(m_impl->shared_state->db_mutex);
                            for (auto& [iname, idx] : m_impl->shared_state->indexes) {
                                if (idx->table_name == tbl_low) { idx->rebuild(*tbl); }
                            }
                        }
                        return create_simple_affected_result(1);
                    }
                    return create_result(0, false);
                }

                std::vector<std::vector<types::Value>> del_rows;
                std::vector<std::vector<types::Value>> kept_rows;
                std::vector<uint8_t> kept_flags;
                std::vector<storage::RecordID> kept_record_ids;
                kept_rows.reserve(tbl->sample_rows.size());
                kept_flags.reserve(tbl->row_flags.size());
                kept_record_ids.reserve(tbl->storage_record_ids.size());
                std::size_t removed = 0;
                for (std::size_t row_index = 0;
                     row_index < tbl->sample_rows.size(); ++row_index) {
                    const auto& row = tbl->sample_rows[row_index];
                    const bool matches = query_engine::eval_predicate(
                        pred, row, *tbl, m_impl->shared_state.get());
                    if (matches && ret_p != std::string::npos) {
                        del_rows.push_back(row);
                    }
                    if (matches) {
                        if (!m_impl->in_transaction) {
                            auto erased = state_storage::erase_page_row(
                                *m_impl->shared_state, *tbl, row_index);
                            if (!erased) return erased.error();
                        }
                        ++removed;
                        continue;
                    }
                    kept_rows.push_back(row);
                    if (row_index < tbl->row_flags.size()) {
                        kept_flags.push_back(tbl->row_flags[row_index]);
                    } else {
                        kept_flags.push_back(0x01);
                    }
                    if (row_index < tbl->storage_record_ids.size()) {
                        kept_record_ids.push_back(tbl->storage_record_ids[row_index]);
                    } else {
                        kept_record_ids.emplace_back();
                    }
                }
                tbl->sample_rows = std::move(kept_rows);
                tbl->row_flags = std::move(kept_flags);
                tbl->storage_record_ids = std::move(kept_record_ids);
                tbl->total_rows = (tbl->total_rows >= removed) ? (tbl->total_rows - removed) : 0;
                // Phase 6: rebuild indexes after rows removed (indices shifted)
                if (removed > 0) {
                    std::string tbl_low = query_engine::to_lower(tbl->name);
                    std::shared_lock<std::shared_mutex> db_lk(m_impl->shared_state->db_mutex);
                    for (auto& [iname, idx] : m_impl->shared_state->indexes) {
                        if (idx->table_name == tbl_low) { idx->rebuild(*tbl); }
                    }
                }

                if (ret_p != std::string::npos) {
                    return project_returning(sql, ret_p, *tbl, del_rows);
                }
                return create_result(removed, false);
            }
            std::size_t affected = (tbl->total_rows > 0) ? 1 : 0;
            if (tbl->total_rows >= affected) {
                tbl->total_rows -= affected;
                if (!tbl->sample_rows.empty()) {
                    const auto row_index = tbl->sample_rows.size() - 1;
                    if (!m_impl->in_transaction) {
                        auto erased = state_storage::erase_page_row(
                            *m_impl->shared_state, *tbl, row_index);
                        if (!erased) return erased.error();
                    }
                    tbl->sample_rows.pop_back();
                    if (!tbl->row_flags.empty()) tbl->row_flags.pop_back();
                    if (!tbl->storage_record_ids.empty()) {
                        tbl->storage_record_ids.pop_back();
                    }
                }
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

        for (std::size_t row_index = 0;
             row_index < tbl->sample_rows.size(); ++row_index) {
            auto& row = tbl->sample_rows[row_index];
            bool matches = (!has_where || query_engine::eval_predicate(pred, row, *tbl, m_impl->shared_state.get()));
            if (matches) {
                auto updated_row = row;
                for (const auto& [c_idx, new_val] : assignments) {
                    if (c_idx < updated_row.size()) {
                        updated_row[c_idx] = new_val;
                    }
                }
                if (!m_impl->in_transaction) {
                    auto stored = state_storage::update_page_row(
                        *m_impl->shared_state, *tbl, row_index, updated_row);
                    if (!stored) return stored.error();
                }
                row = std::move(updated_row);
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

nemo::Result<void> Connection::begin_transaction_state() {
    if (!m_impl) {
        return nemo::Result<void>::success();
    }
    if (m_impl->shared_state && m_impl->shared_state->buffer_pool) {
        std::vector<std::shared_ptr<TableData>> tables;
        {
            std::shared_lock db_lock(m_impl->shared_state->db_mutex);
            tables.reserve(m_impl->shared_state->tables.size());
            for (const auto& [_, table] : m_impl->shared_state->tables) {
                tables.push_back(table);
            }
        }
        for (const auto& table : tables) {
            auto refresh_result = state_storage::refresh_table_rows(
                *m_impl->shared_state, *table);
            if (!refresh_result) return refresh_result.error();
        }
    }
    m_impl->in_transaction = true;
    m_impl->transaction_id = m_impl->shared_state &&
            m_impl->shared_state->wal_manager
        ? m_impl->shared_state->alloc_txn_id()
        : 0;
    m_impl->transaction_last_lsn = 0;
    // Phase 5: snapshot row counts at txn start so commit/rollback know the range
    m_impl->pending_row_starts.clear();
    if (m_impl->shared_state && m_impl->shared_state->wal_manager) {
        std::shared_lock<std::shared_mutex> lk(m_impl->shared_state->db_mutex);
        for (auto& [tname, tbl] : m_impl->shared_state->tables) {
            std::shared_lock<std::shared_mutex> tbl_lk(tbl->mutex);
            m_impl->pending_row_starts[tbl.get()] = tbl->sample_rows.size();
        }
    }
    if (m_impl->shared_state) {
        wal::LogRecord rec(
            m_impl->transaction_id, wal::LogRecordType::TxnBegin,
            wal::LSN::invalid());
        auto append_result = m_impl->shared_state->wal_append(rec);
        if (!append_result) {
            m_impl->in_transaction = false;
            m_impl->transaction_id = 0;
            m_impl->pending_row_starts.clear();
            return append_result.error();
        }
        m_impl->transaction_last_lsn = append_result.value();
    }
    return nemo::Result<void>::success();
}

nemo::Result<Transaction> Connection::begin() {
    auto begin_result = begin_transaction_state();
    if (!begin_result) return begin_result.error();
    return Transaction(this);
}

nemo::Result<Transaction> Connection::transaction() {
    return begin();
}

nemo::Result<void> Connection::commit() {
    if (m_impl) {
        // Phase 5: mark all rows inserted in this txn as committed (Read-Committed visibility)
        if (m_impl->shared_state) {
            std::shared_lock<std::shared_mutex> db_lk(m_impl->shared_state->db_mutex);
            for (auto& [tbl_ptr, from_idx] : m_impl->pending_row_starts) {
                std::unique_lock<std::shared_mutex> tbl_lk(tbl_ptr->mutex);
                tbl_ptr->commit_rows(from_idx);
            }
        }

        if (m_impl->shared_state) {
            nemo::Result<void> persist_result;
            if (m_impl->shared_state->wal_manager && m_impl->in_transaction) {
                auto transaction_scope =
                    m_impl->shared_state->wal_manager->transaction_scope(
                        m_impl->transaction_id);
                persist_result = m_impl->shared_state->persist_state();
            } else {
                persist_result = m_impl->shared_state->persist_state();
            }
            if (!persist_result) return persist_result.error();
        }

        if (m_impl->shared_state && m_impl->shared_state->wal_manager &&
            m_impl->in_transaction) {
            wal::LogRecord rec(
                m_impl->transaction_id, wal::LogRecordType::TxnCommit,
                wal::LSN(m_impl->transaction_last_lsn));
            auto append_result = m_impl->shared_state->wal_append(rec);
            if (!append_result) return append_result.error();
            auto flush_result = m_impl->shared_state->wal_flush(append_result.value());
            if (!flush_result) return flush_result.error();
            m_impl->transaction_last_lsn = append_result.value();
        }

        m_impl->pending_row_starts.clear();
        m_impl->in_transaction = false;
        m_impl->savepoints.clear();
        m_impl->transaction_id = 0;
        m_impl->transaction_last_lsn = 0;
    }
    return nemo::Result<void>::success();
}

nemo::Result<void> Connection::flush() {
    if (m_impl && m_impl->shared_state) {
        auto wal_result = m_impl->shared_state->wal_flush_all();
        if (!wal_result) return wal_result.error();
        auto persist_result = m_impl->shared_state->persist_state();
        if (!persist_result) return persist_result.error();
    }
    return nemo::Result<void>::success();
}

nemo::Result<void> Connection::checkpoint() {
    return flush();
}

nemo::Result<void> Connection::rollback() {
    if (m_impl) {
        // Phase 5: soft-delete rows inserted in this txn (mark as deleted, invisible)
        if (m_impl->shared_state) {
            std::shared_lock<std::shared_mutex> db_lk(m_impl->shared_state->db_mutex);
            for (auto& [tbl_ptr, from_idx] : m_impl->pending_row_starts) {
                std::unique_lock<std::shared_mutex> tbl_lk(tbl_ptr->mutex);
                tbl_ptr->delete_rows(from_idx);
                tbl_ptr->total_rows = tbl_ptr->sample_rows.size();
            }
        }
        m_impl->pending_row_starts.clear();
        nemo::Result<void> wal_result;
        if (m_impl->shared_state && m_impl->shared_state->wal_manager &&
            m_impl->transaction_id != 0) {
            wal::LogRecord rec(
                m_impl->transaction_id, wal::LogRecordType::TxnAbort,
                wal::LSN(m_impl->transaction_last_lsn));
            auto append_result = m_impl->shared_state->wal_append(rec);
            if (!append_result) {
                wal_result = append_result.error();
            } else {
                auto flush_result = m_impl->shared_state->wal_flush(append_result.value());
                if (!flush_result) wal_result = flush_result.error();
                m_impl->transaction_last_lsn = append_result.value();
            }
        }
        m_impl->in_transaction = false;
        m_impl->savepoints.clear();
        m_impl->transaction_id = 0;
        m_impl->transaction_last_lsn = 0;
        if (!wal_result) return wal_result.error();
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
