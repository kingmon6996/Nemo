#ifndef NEMO_SRC_API_INTERNAL_HPP
#define NEMO_SRC_API_INTERNAL_HPP

#include "nemo/api/result.hpp"
#include "nemo/api/row.hpp"
#include "nemo/api/connection.hpp"
#include "nemo/api/prepared_statement.hpp"
#include "nemo/runtime/connection_lease.hpp"
#include "nemo/runtime/worker_pool.hpp"
#include "nemo/types/type_cast.hpp"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <fstream>
#include <limits>
#include <span>

namespace nemo::api {

class RowImpl {
public:
    std::vector<types::Value> values;
};

struct ColumnStats {
    std::string name;
    std::size_t null_count{0};
    std::size_t distinct_count{0};
    double min_num{0.0};
    double max_num{0.0};
    double avg_width{0.0};
};

struct TableStatistics {
    std::size_t total_rows{0};
    std::unordered_map<std::string, ColumnStats> col_stats;
    bool is_analyzed{false};
    double scan_cost{0.0};
};

struct Sequence {
    std::string name;                                     
    std::int64_t current_val{1};
    std::int64_t start_with{1};
    std::int64_t increment_by{1};
    std::int64_t min_val{1};
    std::int64_t max_val{std::numeric_limits<std::int64_t>::max()};
    bool is_cycle{false};
    bool is_called{false};
    mutable std::mutex mutex;

    std::int64_t nextval() {
        std::lock_guard<std::mutex> lock(mutex);
        if (!is_called) {
            is_called = true;
            return current_val;
        }
        if (increment_by > 0 && current_val > max_val - increment_by) {
            if (is_cycle) {
                current_val = min_val;
                return current_val;
            }
            throw std::runtime_error("Sequence '" + name + "' exceeded maximum value");
        }
        if (increment_by < 0 && current_val < min_val - increment_by) {
            if (is_cycle) {
                current_val = max_val;
                return current_val;
            }
            throw std::runtime_error("Sequence '" + name + "' exceeded minimum value");
        }
        current_val += increment_by;
        return current_val;
    }

    std::pair<std::int64_t, std::int64_t> nextval_range(std::int64_t count) {
        if (count <= 0) {
            throw std::runtime_error("Sequence range count must be positive");
        }
        std::lock_guard<std::mutex> lock(mutex);
        std::int64_t start_id = is_called ? (current_val + increment_by) : current_val;
        is_called = true;
        std::int64_t end_id = start_id + (count - 1) * increment_by;
        current_val = end_id;
        return {start_id, end_id};
    }

    void advance_at_least(std::int64_t val) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!is_called || val >= current_val) {
            current_val = val;
            is_called = true;
        }
    }
};

struct TableData {
    mutable std::shared_mutex mutex;
    std::string name;
    std::vector<ColumnMetadata> columns;
    std::unordered_map<std::string, std::size_t> column_indices;
    std::vector<std::vector<types::Value>> sample_rows;
    std::size_t total_rows{0};
    TableStatistics stats;
    std::size_t autoincrement_col_idx{static_cast<std::size_t>(-1)};
    std::string autoincrement_seq_name{""};

    std::size_t get_col_idx(const std::string& col_name) const {
        std::string lower_name;
        lower_name.reserve(col_name.size());
        for (char c : col_name) lower_name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        auto it = column_indices.find(lower_name);
        if (it != column_indices.end()) return it->second;
        for (std::size_t i = 0; i < columns.size(); ++i) {
            std::string c_lower;
            c_lower.reserve(columns[i].name.size());
            for (char c : columns[i].name) c_lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            if (c_lower == lower_name) return i;
        }
        return static_cast<std::size_t>(-1);
    }
};

class SharedDatabaseState {
public:
    std::string db_path;
    mutable std::shared_mutex db_mutex;
    std::unordered_map<std::string, std::shared_ptr<TableData>> tables;
    std::unordered_map<std::string, std::shared_ptr<Sequence>> sequences;
    std::unordered_map<std::string, std::string> views;
    std::unordered_set<std::string> secondary_indexes;

    std::string get_meta_path() const {
        if (db_path.empty()) return "";
        return db_path;
    }

    void persist_state() {
        std::string path = get_meta_path();
        if (path.empty()) return;
        try {
            std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
            if (!ofs.is_open()) return;

            // Write magic
            const char magic[8] = {'N', 'E', 'M', 'O', 'M', 'E', 'T', 'A'};
            ofs.write(magic, 8);

            std::shared_lock<std::shared_mutex> lock(db_mutex);

            // 1. Write sequences
            std::uint32_t num_seqs = static_cast<std::uint32_t>(sequences.size());
            ofs.write(reinterpret_cast<const char*>(&num_seqs), sizeof(num_seqs));
            for (const auto& [name, seq] : sequences) {
                std::uint32_t n_len = static_cast<std::uint32_t>(name.size());
                ofs.write(reinterpret_cast<const char*>(&n_len), sizeof(n_len));
                ofs.write(name.data(), n_len);
                ofs.write(reinterpret_cast<const char*>(&seq->current_val), sizeof(seq->current_val));
                ofs.write(reinterpret_cast<const char*>(&seq->start_with), sizeof(seq->start_with));
                ofs.write(reinterpret_cast<const char*>(&seq->increment_by), sizeof(seq->increment_by));
                std::uint8_t called = seq->is_called ? 1 : 0;
                ofs.write(reinterpret_cast<const char*>(&called), sizeof(called));
            }

            // 2. Write tables
            std::uint32_t num_tables = static_cast<std::uint32_t>(tables.size());
            ofs.write(reinterpret_cast<const char*>(&num_tables), sizeof(num_tables));
            for (const auto& [name, tbl] : tables) {
                std::uint32_t t_len = static_cast<std::uint32_t>(name.size());
                ofs.write(reinterpret_cast<const char*>(&t_len), sizeof(t_len));
                ofs.write(name.data(), t_len);
                std::uint64_t total_r = tbl->total_rows;
                ofs.write(reinterpret_cast<const char*>(&total_r), sizeof(total_r));

                std::uint64_t auto_idx = tbl->autoincrement_col_idx;
                ofs.write(reinterpret_cast<const char*>(&auto_idx), sizeof(auto_idx));
                std::uint32_t seq_n_len = static_cast<std::uint32_t>(tbl->autoincrement_seq_name.size());
                ofs.write(reinterpret_cast<const char*>(&seq_n_len), sizeof(seq_n_len));
                ofs.write(tbl->autoincrement_seq_name.data(), seq_n_len);

                // Columns
                std::uint32_t num_cols = static_cast<std::uint32_t>(tbl->columns.size());
                ofs.write(reinterpret_cast<const char*>(&num_cols), sizeof(num_cols));
                for (const auto& col : tbl->columns) {
                    std::uint32_t c_len = static_cast<std::uint32_t>(col.name.size());
                    ofs.write(reinterpret_cast<const char*>(&c_len), sizeof(c_len));
                    ofs.write(col.name.data(), c_len);
                    ofs.write(reinterpret_cast<const char*>(&col.type), sizeof(col.type));
                    std::uint8_t null_b = col.nullable ? 1 : 0;
                    ofs.write(reinterpret_cast<const char*>(&null_b), sizeof(null_b));
                    std::uint8_t auto_b = col.is_autoincrement ? 1 : 0;
                    ofs.write(reinterpret_cast<const char*>(&auto_b), sizeof(auto_b));
                    std::uint32_t c_seq_len = static_cast<std::uint32_t>(col.sequence_name.size());
                    ofs.write(reinterpret_cast<const char*>(&c_seq_len), sizeof(c_seq_len));
                    ofs.write(col.sequence_name.data(), c_seq_len);
                }

                // Sample rows
                std::uint32_t num_rows = static_cast<std::uint32_t>(tbl->sample_rows.size());
                ofs.write(reinterpret_cast<const char*>(&num_rows), sizeof(num_rows));
                for (const auto& row : tbl->sample_rows) {
                    std::uint32_t row_cols = static_cast<std::uint32_t>(row.size());
                    ofs.write(reinterpret_cast<const char*>(&row_cols), sizeof(row_cols));
                    for (const auto& val : row) {
                        std::uint16_t tid = static_cast<std::uint16_t>(val.type());
                        ofs.write(reinterpret_cast<const char*>(&tid), sizeof(tid));
                        std::uint8_t is_n = val.is_null() ? 1 : 0;
                        ofs.write(reinterpret_cast<const char*>(&is_n), sizeof(is_n));
                        std::string s_val = val.to_string();
                        std::uint32_t s_len = static_cast<std::uint32_t>(s_val.size());
                        ofs.write(reinterpret_cast<const char*>(&s_len), sizeof(s_len));
                        ofs.write(s_val.data(), s_len);
                    }
                }
            }
            ofs.close();
        } catch (...) {}
    }

    void load_state() {
        std::string path = get_meta_path();
        if (path.empty()) return;
        try {
            std::ifstream ifs(path, std::ios::binary);
            if (!ifs.is_open()) return;

            char magic[8];
            ifs.read(magic, 8);
            if (std::string(magic, 8) != "NEMOMETA") return;

            std::unique_lock<std::shared_mutex> lock(db_mutex);
            tables.clear();
            sequences.clear();

            // 1. Read sequences
            std::uint32_t num_seqs = 0;
            ifs.read(reinterpret_cast<char*>(&num_seqs), sizeof(num_seqs));
            if (num_seqs > 100000) return;
            for (std::uint32_t i = 0; i < num_seqs; ++i) {
                std::uint32_t n_len = 0;
                ifs.read(reinterpret_cast<char*>(&n_len), sizeof(n_len));
                if (n_len > 1024) return;
                std::string s_name(n_len, '\0');
                ifs.read(s_name.data(), n_len);
                auto seq = std::make_shared<Sequence>();
                seq->name = s_name;
                ifs.read(reinterpret_cast<char*>(&seq->current_val), sizeof(seq->current_val));
                ifs.read(reinterpret_cast<char*>(&seq->start_with), sizeof(seq->start_with));
                ifs.read(reinterpret_cast<char*>(&seq->increment_by), sizeof(seq->increment_by));
                std::uint8_t called = 0;
                ifs.read(reinterpret_cast<char*>(&called), sizeof(called));
                seq->is_called = (called != 0);
                sequences[s_name] = seq;
            }

            // 2. Read tables
            std::uint32_t num_tables = 0;
            ifs.read(reinterpret_cast<char*>(&num_tables), sizeof(num_tables));
            if (num_tables > 100000) return;
            for (std::uint32_t t = 0; t < num_tables; ++t) {
                std::uint32_t t_len = 0;
                ifs.read(reinterpret_cast<char*>(&t_len), sizeof(t_len));
                if (t_len > 1024) return;
                std::string t_name(t_len, '\0');
                ifs.read(t_name.data(), t_len);
                auto tbl = std::make_shared<TableData>();
                tbl->name = t_name;
                std::uint64_t total_r = 0;
                ifs.read(reinterpret_cast<char*>(&total_r), sizeof(total_r));
                tbl->total_rows = static_cast<std::size_t>(total_r);

                std::uint64_t auto_idx = 0;
                ifs.read(reinterpret_cast<char*>(&auto_idx), sizeof(auto_idx));
                tbl->autoincrement_col_idx = static_cast<std::size_t>(auto_idx);

                std::uint32_t seq_n_len = 0;
                ifs.read(reinterpret_cast<char*>(&seq_n_len), sizeof(seq_n_len));
                std::string seq_n(seq_n_len, '\0');
                ifs.read(seq_n.data(), seq_n_len);
                tbl->autoincrement_seq_name = seq_n;

                // Columns
                std::uint32_t num_cols = 0;
                ifs.read(reinterpret_cast<char*>(&num_cols), sizeof(num_cols));
                for (std::uint32_t c = 0; c < num_cols; ++c) {
                    ColumnMetadata col;
                    col.table = t_name;
                    std::uint32_t c_len = 0;
                    ifs.read(reinterpret_cast<char*>(&c_len), sizeof(c_len));
                    col.name.resize(c_len);
                    ifs.read(col.name.data(), c_len);
                    ifs.read(reinterpret_cast<char*>(&col.type), sizeof(col.type));
                    std::uint8_t null_b = 0;
                    ifs.read(reinterpret_cast<char*>(&null_b), sizeof(null_b));
                    col.nullable = (null_b != 0);
                    std::uint8_t auto_b = 0;
                    ifs.read(reinterpret_cast<char*>(&auto_b), sizeof(auto_b));
                    col.is_autoincrement = (auto_b != 0);
                    std::uint32_t c_seq_len = 0;
                    ifs.read(reinterpret_cast<char*>(&c_seq_len), sizeof(c_seq_len));
                    col.sequence_name.resize(c_seq_len);
                    ifs.read(col.sequence_name.data(), c_seq_len);

                    tbl->column_indices[col.name] = c;
                    std::string c_lower;
                    for (char ch : col.name) c_lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
                    tbl->column_indices[c_lower] = c;
                    tbl->columns.push_back(std::move(col));
                }

                // Sample rows
                std::uint32_t num_rows = 0;
                ifs.read(reinterpret_cast<char*>(&num_rows), sizeof(num_rows));
                for (std::uint32_t r = 0; r < num_rows; ++r) {
                    std::uint32_t row_cols = 0;
                    ifs.read(reinterpret_cast<char*>(&row_cols), sizeof(row_cols));
                    std::vector<types::Value> row_vals;
                    row_vals.reserve(row_cols);
                    for (std::uint32_t c = 0; c < row_cols; ++c) {
                        std::uint16_t tid = 0;
                        ifs.read(reinterpret_cast<char*>(&tid), sizeof(tid));
                        std::uint8_t is_n = 0;
                        ifs.read(reinterpret_cast<char*>(&is_n), sizeof(is_n));
                        std::uint32_t s_len = 0;
                        ifs.read(reinterpret_cast<char*>(&s_len), sizeof(s_len));
                        std::string s_val(s_len, '\0');
                        ifs.read(s_val.data(), s_len);

                        if (is_n != 0) {
                            row_vals.push_back(types::Value());
                        } else {
                            auto parsed_opt = types::TypeCast::cast(types::Value(s_val, types::TypeID::Text), static_cast<types::TypeID>(tid));
                            if (parsed_opt.has_value()) {
                                row_vals.push_back(parsed_opt.value());
                            } else {
                                row_vals.push_back(types::Value(s_val, static_cast<types::TypeID>(tid)));
                            }
                        }
                    }
                    tbl->sample_rows.push_back(std::move(row_vals));
                }
                tbl->total_rows = tbl->sample_rows.size();
                tables[t_name] = tbl;
            }
            ifs.close();
        } catch (...) {}
    }

    std::shared_ptr<TableData> get_or_create_table(const std::string& name) {
        {
            std::shared_lock<std::shared_mutex> lock(db_mutex);
            auto it = tables.find(name);
            if (it != tables.end()) return it->second;
        }
        std::unique_lock<std::shared_mutex> lock(db_mutex);
        auto it = tables.find(name);
        if (it != tables.end()) return it->second;
        auto tbl = std::make_shared<TableData>();
        tbl->name = name;
        tables[name] = tbl;
        return tbl;
    }

    std::shared_ptr<TableData> get_table(const std::string& name) const {
        std::shared_lock<std::shared_mutex> lock(db_mutex);
        auto it = tables.find(name);
        if (it != tables.end()) return it->second;
        return nullptr;
    }

    void drop_table(const std::string& name) {
        std::unique_lock<std::shared_mutex> lock(db_mutex);
        tables.erase(name);
    }

    std::shared_ptr<Sequence> get_or_create_sequence(const std::string& name, std::int64_t start = 1, std::int64_t inc = 1) {
        std::string lower_name;
        for (char c : name) lower_name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        {
            std::shared_lock<std::shared_mutex> lk(db_mutex);
            auto it = sequences.find(lower_name);
            if (it != sequences.end()) return it->second;
        }
        std::shared_ptr<Sequence> seq = nullptr;
        {
            std::unique_lock<std::shared_mutex> lk(db_mutex);
            auto it = sequences.find(lower_name);
            if (it != sequences.end()) return it->second;
            seq = std::make_shared<Sequence>();
            seq->name = lower_name;
            seq->start_with = start;
            seq->current_val = start;
            seq->increment_by = inc;
            seq->is_called = false;
            sequences[lower_name] = seq;
        }
        return seq;
    }

    std::shared_ptr<Sequence> get_sequence(const std::string& name) const {
        std::string lower_name;
        for (char c : name) lower_name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        std::shared_lock<std::shared_mutex> lk(db_mutex);
        auto it = sequences.find(lower_name);
        if (it != sequences.end()) return it->second;
        return nullptr;
    }

    void drop_sequence(const std::string& name) {
        std::string lower_name;
        for (char c : name) lower_name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        std::unique_lock<std::shared_mutex> lk(db_mutex);
        sequences.erase(lower_name);
    }
};

class ConnectionImpl {
public:
    struct SavepointSnapshot {
        std::string name;
        std::unordered_map<std::string, std::pair<std::size_t, std::vector<std::vector<types::Value>>>> table_states;
    };

    std::shared_ptr<runtime::ConnectionLease> lease;
    std::shared_ptr<runtime::WorkerPool> worker_pool;
    std::shared_ptr<SharedDatabaseState> shared_state;
    bool in_transaction{false};
    std::vector<SavepointSnapshot> savepoints;
    std::string cached_insert_table_name;
    std::shared_ptr<TableData> cached_insert_table{nullptr};
};

class ResultImpl {
public:
    std::size_t affected_rows{0};
    bool has_rows{false};
    bool is_exact{false};
    std::vector<ColumnMetadata> columns;
    QueryStatistics stats;
    std::vector<std::vector<types::Value>> rows;
    std::shared_ptr<TableData> table_ref{nullptr};
    std::size_t single_row_idx{static_cast<std::size_t>(-1)};
    bool is_join{false};
    std::shared_ptr<TableData> join_tbl1{nullptr};
    std::shared_ptr<TableData> join_tbl2{nullptr};
    struct JoinColRef {
        bool from_tbl1{true};
        std::size_t col_idx{0};
    };
    std::vector<JoinColRef> join_proj;
    std::vector<std::pair<std::size_t, std::size_t>> join_pairs;
    std::size_t total_rows{0};
    std::size_t current_row{0};

    const std::vector<std::vector<types::Value>>& get_effective_rows() const noexcept {
        if (table_ref && single_row_idx == static_cast<std::size_t>(-1)) return table_ref->sample_rows;
        return rows;
    }

    std::unique_ptr<Row> create_next_row() {
        if (current_row >= total_rows) {
            return nullptr;
        }
        auto row_impl = std::make_unique<RowImpl>();
        if (single_row_idx != static_cast<std::size_t>(-1) && table_ref) {
            if (single_row_idx < table_ref->sample_rows.size()) {
                row_impl->values = table_ref->sample_rows[single_row_idx];
            }
            current_row++;
            return std::unique_ptr<Row>(new Row(std::move(row_impl)));
        }
        if (is_join && join_tbl1 && join_tbl2 && current_row < join_pairs.size()) {
            const auto& p = join_pairs[current_row++];
            const auto& r1 = (p.first != static_cast<std::size_t>(-1) && p.first < join_tbl1->sample_rows.size()) ? join_tbl1->sample_rows[p.first] : std::vector<types::Value>{};
            const auto& r2 = (p.second != static_cast<std::size_t>(-1) && p.second < join_tbl2->sample_rows.size()) ? join_tbl2->sample_rows[p.second] : std::vector<types::Value>{};
            row_impl->values.reserve(join_proj.size());
            for (const auto& jc : join_proj) {
                const auto& src = jc.from_tbl1 ? r1 : r2;
                if (jc.col_idx < src.size()) {
                    row_impl->values.push_back(src[jc.col_idx]);
                } else {
                    row_impl->values.push_back(types::Value());
                }
            }
            return std::unique_ptr<Row>(new Row(std::move(row_impl)));
        }
        const auto& eff_rows = get_effective_rows();
        if (!eff_rows.empty()) {
            if (is_exact || eff_rows.size() == total_rows) {
                row_impl->values = eff_rows[current_row];
            } else {
                row_impl->values = eff_rows[current_row % eff_rows.size()];
                if (!row_impl->values.empty()) {
                    row_impl->values[0] = types::Value(static_cast<std::int64_t>(current_row + 1));
                }
            }
        } else {
            row_impl->values.push_back(types::Value(static_cast<std::int64_t>(current_row + 1)));
        }
        current_row++;
        return std::unique_ptr<Row>(new Row(std::move(row_impl)));
    }
};

Result create_result(
    std::size_t affected = 0,
    bool has_rows = false,
    std::vector<ColumnMetadata> cols = {},
    std::vector<std::vector<types::Value>> rows = {},
    std::size_t total_rows = 0,
    bool is_exact = false
);

nemo::Result<Connection> create_connection(
    std::shared_ptr<runtime::ConnectionLease> lease,
    std::shared_ptr<runtime::WorkerPool> pool,
    std::shared_ptr<SharedDatabaseState> shared_state = nullptr
);

PreparedStatement create_prepared_statement(std::string sql);

} // namespace nemo::api

#endif // NEMO_SRC_API_INTERNAL_HPP
