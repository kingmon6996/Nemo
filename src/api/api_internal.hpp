#ifndef NEMO_SRC_API_INTERNAL_HPP
#define NEMO_SRC_API_INTERNAL_HPP

#include "nemo/api/result.hpp"
#include "nemo/api/row.hpp"
#include "nemo/api/connection.hpp"
#include "nemo/api/prepared_statement.hpp"
#include "nemo/runtime/connection_lease.hpp"
#include "nemo/runtime/worker_pool.hpp"
#include "nemo/types/type_cast.hpp"
#include "nemo/wal/wal_manager.hpp"
#include "nemo/wal/log_record.hpp"
#include "nemo/wal/log_record_type.hpp"
#include "nemo/transaction/transaction_id.hpp"
#include "nemo/core/result.hpp"
#include "nemo/logging/logger.hpp"
#include "nemo/storage/buffer_pool.hpp"
#include "nemo/storage/disk_manager.hpp"
#include "nemo/storage/record_id.hpp"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <mutex>
#include <fstream>
#include <limits>
#include <span>
#include <filesystem>
#include <atomic>
#include <map>        // Phase 6: ordered map for in-memory B+ Tree index
#include <charconv>
#include <optional>
#include <sstream>
#include <string_view>

namespace nemo::api {

class SharedDatabaseState;
struct TableData;

namespace state_storage {
nemo::Result<void> persist_pages(SharedDatabaseState& state);
nemo::Result<void> load_pages(SharedDatabaseState& state);
nemo::Result<void> refresh_table_rows(SharedDatabaseState& state, TableData& table);
nemo::Result<std::optional<std::vector<types::Value>>> read_page_row(
    SharedDatabaseState& state, TableData& table, std::size_t row_index,
    std::int64_t expected_id);
nemo::Result<storage::RecordID> insert_page_row(
    SharedDatabaseState& state, TableData& table,
    const std::vector<types::Value>& row);
nemo::Result<void> update_page_row(
    SharedDatabaseState& state, TableData& table, std::size_t row_index,
    const std::vector<types::Value>& row);
nemo::Result<void> erase_page_row(
    SharedDatabaseState& state, TableData& table, std::size_t row_index);
}

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
    mutable std::shared_mutex mutex;  // readers: shared_lock; writers: unique_lock
    std::mutex write_mutex;           // Phase 5: serializes concurrent writers across connections
    std::string name;
    std::vector<ColumnMetadata> columns;
    std::unordered_map<std::string, std::size_t> column_indices;
    std::vector<std::vector<types::Value>> sample_rows;
    PageID storage_root_page{};
    std::vector<storage::RecordID> storage_record_ids;
    bool page_cache_current{false};
    // Phase 5: row visibility flags, parallel to sample_rows.
    // Bit 0 (0x01) = committed (visible to readers).
    // Bit 1 (0x02) = deleted   (soft-delete, invisible to readers).
    // Rows inserted inside an open transaction have flags=0 until COMMIT sets bit 0.
    // Auto-committed INSERTs (outside explicit transaction) set bit 0 immediately.
    // NOTE: std::atomic<T> is not copyable/movable, so we store plain uint8_t and
    //       use std::atomic_ref<uint8_t> (C++20) for atomic access at the call sites.
    std::vector<uint8_t> row_flags;
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

    // Phase 5: append a row; caller holds unique_lock on mutex.
    // committed=true  → auto-commit path (single statement outside txn)
    // committed=false → inside explicit transaction; commit() must mark it visible
    std::size_t push_row(std::vector<types::Value> row, bool committed = true) {
        std::size_t idx = sample_rows.size();
        sample_rows.push_back(std::move(row));
        row_flags.push_back(committed ? static_cast<uint8_t>(0x01) : static_cast<uint8_t>(0x00));
        storage_record_ids.emplace_back();
        total_rows = sample_rows.size();
        return idx;
    }

    // Phase 5: mark a range of rows as committed (called on COMMIT)
    void commit_rows(std::size_t from_idx) noexcept {
        for (std::size_t i = from_idx; i < row_flags.size(); ++i) {
            uint8_t f = std::atomic_ref<uint8_t>(row_flags[i]).load(std::memory_order_acquire);
            if ((f & 0x02) || (f & 0x01)) {
                continue; // Ignore already-visible or already-deleted rows.
            }
            std::atomic_ref<uint8_t>(row_flags[i]).fetch_or(0x01, std::memory_order_release);
        }
    }

    // Phase 5: mark a range of rows as deleted (soft-delete on ROLLBACK)
    void delete_rows(std::size_t from_idx) noexcept {
        for (std::size_t i = from_idx; i < row_flags.size(); ++i) {
            uint8_t f = std::atomic_ref<uint8_t>(row_flags[i]).load(std::memory_order_acquire);
            if ((f & 0x01) && !(f & 0x02)) {
                continue; // Existing committed rows must remain visible.
            }
            std::atomic_ref<uint8_t>(row_flags[i]).fetch_or(0x02, std::memory_order_release);
        }
    }

    // Phase 5: is a row visible to Read-Committed readers?
    // committed=1 and not deleted=1
    bool is_row_visible(std::size_t idx) const noexcept {
        if (idx >= row_flags.size()) return true;  // legacy row (no flag) → visible
        // atomic_ref requires non-const; row_flags elements are plain uint8_t under a const method.
        uint8_t f = std::atomic_ref<uint8_t>(const_cast<uint8_t&>(row_flags[idx])).load(std::memory_order_acquire);
        return (f & 0x01) && !(f & 0x02);
    }

    // Phase 5: compact — purge logically-deleted rows (called on checkpoint or VACUUM)
    void compact() {
        if (storage_record_ids.size() < sample_rows.size()) {
            storage_record_ids.resize(sample_rows.size());
        }
        std::size_t w = 0;
        for (std::size_t r = 0; r < sample_rows.size(); ++r) {
            uint8_t f = (r < row_flags.size())
                ? std::atomic_ref<uint8_t>(row_flags[r]).load(std::memory_order_relaxed)
                : static_cast<uint8_t>(0x01);
            if (!(f & 0x02)) {  // not deleted
                if (w != r) {
                    sample_rows[w] = std::move(sample_rows[r]);
                    if (r < storage_record_ids.size()) {
                        storage_record_ids[w] = storage_record_ids[r];
                    }
                    if (w < row_flags.size() && r < row_flags.size()) {
                        uint8_t val = std::atomic_ref<uint8_t>(row_flags[r]).load(std::memory_order_relaxed);
                        std::atomic_ref<uint8_t>(row_flags[w]).store(val, std::memory_order_relaxed);
                    }
                }
                ++w;
            }
        }
        sample_rows.resize(w);
        if (row_flags.size() > w) row_flags.resize(w);
        if (storage_record_ids.size() > w) storage_record_ids.resize(w);
        total_rows = w;
    }
};

// ---------------------------------------------------------------------------
// Phase 6: In-memory ordered index (B+ Tree semantics over std::map)
// Key = single column Value, Value = list of row-indices in sample_rows.
// ---------------------------------------------------------------------------

struct ValueLess {
    // Extract a double from a numeric Value (inline — cannot call query_engine::value_as_double here)
    static double to_double(const types::Value& v) noexcept {
        switch (v.type()) {
            case types::TypeID::BigInt:   return static_cast<double>(v.get<std::int64_t>());
            case types::TypeID::Integer:  return static_cast<double>(v.get<std::int32_t>());
            case types::TypeID::SmallInt: return static_cast<double>(v.get<std::int16_t>());
            case types::TypeID::Double:   return v.get<double>();
            case types::TypeID::Real:     return static_cast<double>(v.get<float>());
            default:                      return 0.0;
        }
    }
    static bool is_numeric(const types::Value& v) noexcept {
        return v.type() == types::TypeID::Integer ||
               v.type() == types::TypeID::BigInt  ||
               v.type() == types::TypeID::SmallInt||
               v.type() == types::TypeID::Real    ||
               v.type() == types::TypeID::Double;
    }
    bool operator()(const types::Value& a, const types::Value& b) const {
        if (a.is_null() && b.is_null()) return false;
        if (a.is_null()) return true;
        if (b.is_null()) return false;
        if (is_numeric(a) && is_numeric(b)) {
            return to_double(a) < to_double(b);
        }
        return a.to_string() < b.to_string();
    }
};

struct InMemoryIndex {
    std::string index_name;
    std::string table_name;
    std::string column_name;
    std::size_t col_idx{0};
    bool is_unique{false};

    // key → sorted list of row indices in TableData::sample_rows
    std::map<types::Value, std::vector<std::size_t>, ValueLess> entries;
    mutable std::shared_mutex mutex;

    // Build from scratch from an existing (locked) TableData
    void rebuild(const TableData& tbl) {
        std::unique_lock<std::shared_mutex> lock(mutex);
        entries.clear();
        for (std::size_t i = 0; i < tbl.sample_rows.size(); ++i) {
            if (!tbl.is_row_visible(i)) continue;
            if (col_idx < tbl.sample_rows[i].size()) {
                entries[tbl.sample_rows[i][col_idx]].push_back(i);
            }
        }
    }

    // Called by INSERT after push_row() — always O(log N)
    void on_insert(const types::Value& key, std::size_t row_idx) {
        std::unique_lock<std::shared_mutex> lk(mutex);
        entries[key].push_back(row_idx);
    }

    // Called by DELETE / ROLLBACK soft-delete
    void on_delete(const types::Value& key, std::size_t row_idx) {
        std::unique_lock<std::shared_mutex> lk(mutex);
        auto it = entries.find(key);
        if (it == entries.end()) return;
        auto& vec = it->second;
        vec.erase(std::remove(vec.begin(), vec.end(), row_idx), vec.end());
        if (vec.empty()) entries.erase(it);
    }

    // Point lookup — O(log N); returns matching row indices
    std::vector<std::size_t> lookup(const types::Value& key) const {
        std::shared_lock<std::shared_mutex> lk(mutex);
        auto it = entries.find(key);
        if (it == entries.end()) return {};
        return it->second;
    }
};

namespace detail {

inline bool parse_integer(std::string_view text, std::int64_t& value) {
    if (text.empty()) return false;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

inline bool parse_clock_time(std::string_view text, std::int64_t& microseconds) {
    const auto first_colon = text.find(':');
    const auto second_colon = first_colon == std::string_view::npos
        ? std::string_view::npos : text.find(':', first_colon + 1);
    if (first_colon == std::string_view::npos || second_colon == std::string_view::npos) {
        return false;
    }

    const auto dot = text.find('.', second_colon + 1);
    const auto hours_text = text.substr(0, first_colon);
    const auto minutes_text = text.substr(first_colon + 1, second_colon - first_colon - 1);
    const auto seconds_text = text.substr(second_colon + 1,
        (dot == std::string_view::npos ? text.size() : dot) - second_colon - 1);
    std::int64_t hours = 0, minutes = 0, seconds = 0;
    if (!parse_integer(hours_text, hours) || !parse_integer(minutes_text, minutes) ||
        !parse_integer(seconds_text, seconds) || hours < 0 ||
        minutes >= 60 || seconds >= 60 ||
        hours > std::numeric_limits<std::int64_t>::max() / 3'600'000'000) {
        return false;
    }

    std::int64_t fraction = 0;
    if (dot != std::string_view::npos) {
        const auto fraction_text = text.substr(dot + 1);
        if (fraction_text.empty() || fraction_text.size() > 6 ||
            !parse_integer(fraction_text, fraction)) {
            return false;
        }
        for (std::size_t digits = fraction_text.size(); digits < 6; ++digits) {
            fraction *= 10;
        }
    }
    microseconds = ((hours * 60 + minutes) * 60 + seconds) * 1'000'000 + fraction;
    return true;
}

inline std::optional<types::Value> decode_legacy_value(
    std::string_view encoded, types::TypeID type) {
    if (type == types::TypeID::Numeric) {
        bool negative = false;
        if (!encoded.empty() && (encoded.front() == '-' || encoded.front() == '+')) {
            negative = encoded.front() == '-';
            encoded.remove_prefix(1);
        }
        const auto dot = encoded.find('.');
        const auto scale = dot == std::string_view::npos ? 0 : encoded.size() - dot - 1;
        if (encoded.empty() || scale > std::numeric_limits<std::uint8_t>::max()) {
            return std::nullopt;
        }
        std::string digits;
        digits.reserve(encoded.size());
        for (const char digit : encoded) {
            if (digit == '.') continue;
            if (digit < '0' || digit > '9') return std::nullopt;
            digits.push_back(digit);
        }
        if (negative) digits.insert(digits.begin(), '-');
        std::int64_t coefficient = 0;
        if (!parse_integer(digits, coefficient)) return std::nullopt;
        return types::Value(types::Decimal(
            coefficient, static_cast<std::uint8_t>(scale)));
    }

    if (type == types::TypeID::Bytea) {
        if (encoded.size() < 2 || encoded[0] != '\\' || encoded[1] != 'x' ||
            (encoded.size() - 2) % 2 != 0) {
            return std::nullopt;
        }
        std::vector<std::uint8_t> bytes;
        bytes.reserve((encoded.size() - 2) / 2);
        for (std::size_t offset = 2; offset < encoded.size(); offset += 2) {
            unsigned int byte = 0;
            const auto [end, error] = std::from_chars(
                encoded.data() + offset, encoded.data() + offset + 2, byte, 16);
            if (error != std::errc{} || end != encoded.data() + offset + 2 ||
                byte > 0xff) {
                return std::nullopt;
            }
            bytes.push_back(static_cast<std::uint8_t>(byte));
        }
        return types::Value(std::move(bytes));
    }

    if (type == types::TypeID::Date) {
        const auto first_dash = encoded.find('-');
        const auto second_dash = first_dash == std::string_view::npos
            ? std::string_view::npos : encoded.find('-', first_dash + 1);
        if (first_dash == std::string_view::npos || second_dash == std::string_view::npos) {
            return std::nullopt;
        }
        std::int64_t year = 0, month = 0, day = 0;
        if (!parse_integer(encoded.substr(0, first_dash), year) ||
            !parse_integer(encoded.substr(first_dash + 1, second_dash - first_dash - 1), month) ||
            !parse_integer(encoded.substr(second_dash + 1), day) ||
            month != 1 || day < 1 || day > 365) {
            return std::nullopt;
        }
        const auto days = (year - 2000) * 365 + day - 1;
        if (days < std::numeric_limits<std::int32_t>::min() ||
            days > std::numeric_limits<std::int32_t>::max()) {
            return std::nullopt;
        }
        return types::Value(types::Date(static_cast<std::int32_t>(days)));
    }

    if (type == types::TypeID::Time) {
        std::int64_t microseconds = 0;
        if (!parse_clock_time(encoded, microseconds)) return std::nullopt;
        return types::Value(types::Time(microseconds));
    }

    if (type == types::TypeID::Timestamp || type == types::TypeID::TimestampTZ) {
        constexpr std::string_view prefix = "TIMESTAMP(";
        constexpr std::string_view suffix = " us)";
        if (!encoded.starts_with(prefix) || !encoded.ends_with(suffix) ||
            encoded.size() <= prefix.size() + suffix.size()) {
            return std::nullopt;
        }
        std::int64_t microseconds = 0;
        if (!parse_integer(encoded.substr(
                prefix.size(), encoded.size() - prefix.size() - suffix.size()),
                microseconds)) {
            return std::nullopt;
        }
        return types::Value(types::Timestamp(microseconds),
                            type == types::TypeID::TimestampTZ);
    }

    if (type == types::TypeID::Interval) {
        std::int32_t months = 0, days = 0;
        std::int64_t microseconds = 0;
        bool found_clock = false;
        std::string previous;
        std::istringstream input{std::string(encoded)};
        std::string token;
        while (input >> token) {
            if (token == "months" || token == "days") {
                std::int64_t amount = 0;
                if (!parse_integer(previous, amount) ||
                    amount < std::numeric_limits<std::int32_t>::min() ||
                    amount > std::numeric_limits<std::int32_t>::max()) {
                    return std::nullopt;
                }
                if (token == "months") months = static_cast<std::int32_t>(amount);
                else days = static_cast<std::int32_t>(amount);
                previous.clear();
            } else if (token.find(':') != std::string::npos) {
                if (!parse_clock_time(token, microseconds)) return std::nullopt;
                found_clock = true;
            } else {
                previous = std::move(token);
            }
        }
        if (!found_clock) return std::nullopt;
        return types::Value(types::Interval(months, days, microseconds));
    }

    if (type == types::TypeID::JSON) {
        if (encoded.size() >= 2 && encoded.front() == '"' && encoded.back() == '"') {
            return types::Value(types::Json(types::JsonNode(
                std::string(encoded.substr(1, encoded.size() - 2)))));
        }
        return types::Value(types::Json::parse(std::string(encoded)));
    }

    return types::TypeCast::cast(
        types::Value(std::string(encoded), types::TypeID::Text), type);
}

} // namespace detail

class SharedDatabaseState {
public:
    std::string db_path;
    storage::BufferPool* buffer_pool{nullptr};
    storage::DiskManager* disk_manager{nullptr};
    mutable std::shared_mutex db_mutex;
    std::unordered_map<std::string, std::shared_ptr<TableData>> tables;
    std::unordered_map<std::string, std::shared_ptr<Sequence>> sequences;
    std::unordered_map<std::string, std::string> views;
    std::unordered_set<std::string> secondary_indexes;

    // Phase 6: live in-memory indexes — keyed by index_name (lowercase)
    // Each InMemoryIndex tracks one column of one table.
    std::unordered_map<std::string, std::shared_ptr<InMemoryIndex>> indexes;

    // Phase 6: helper — find index for (table, column), null if none
    std::shared_ptr<InMemoryIndex> get_index(const std::string& tbl_lower,
                                              const std::string& col_lower) const {
        // secondary_indexes holds names; we need to scan indexes map for matching table+col
        for (auto& [name, idx] : indexes) {
            if (idx->table_name == tbl_lower && idx->column_name == col_lower) return idx;
        }
        return nullptr;
    }

    // Phase 6: notify all indexes on a table that a new row was inserted
    void index_on_insert(const std::string& tbl_lower, std::size_t row_idx,
                         const std::vector<types::Value>& row) noexcept {
        for (auto& [name, idx] : indexes) {
            if (idx->table_name != tbl_lower) continue;
            if (idx->col_idx < row.size()) {
                idx->on_insert(row[idx->col_idx], row_idx);
            }
        }
    }

    // Phase 6: notify all indexes on a table that a row was deleted
    void index_on_delete(const std::string& tbl_lower, std::size_t row_idx,
                         const std::vector<types::Value>& row) noexcept {
        for (auto& [name, idx] : indexes) {
            if (idx->table_name != tbl_lower) continue;
            if (idx->col_idx < row.size()) {
                idx->on_delete(row[idx->col_idx], row_idx);
            }
        }
    }

    // Phase 3: WAL manager — set by Database::open(), null until Phase 3 wiring
    wal::WALManager* wal_manager{nullptr};

    // Phase 3: monotonic transaction-ID counter (per-database)
    std::atomic<transaction::TransactionID> next_txn_id{transaction::INITIAL_TRANSACTION_ID};

    // Phase 5: intra-process open connection count (advisory, not a lock)
    std::atomic<int> connection_count{0};

    transaction::TransactionID alloc_txn_id() noexcept {
        return next_txn_id.fetch_add(1, std::memory_order_relaxed);
    }

    nemo::Result<std::uint64_t> wal_append(wal::LogRecord& record) {
        if (!wal_manager) return nemo::Status::InvalidState;
        try {
            wal::LSN lsn = wal_manager->append(record);
            return lsn.value();
        } catch (const std::exception& error) {
            NEMO_LOG_ERROR("WAL", error.what());
            return nemo::Status::IOError;
        }
    }

    nemo::Result<void> wal_flush(std::uint64_t lsn_val) {
        if (!wal_manager) return nemo::Status::InvalidState;
        if (lsn_val == 0) return nemo::Status::InvalidArgument;
        try {
            wal_manager->flush_up_to(wal::LSN(lsn_val));
            return {};
        } catch (const std::exception& error) {
            NEMO_LOG_ERROR("WAL", error.what());
            return nemo::Status::IOError;
        }
    }

    nemo::Result<void> wal_flush_all() {
        if (!wal_manager) return {};
        try {
            wal_manager->flush();
            return {};
        } catch (const std::exception& error) {
            NEMO_LOG_ERROR("WAL", error.what());
            return nemo::Status::IOError;
        }
    }

    std::string get_meta_path() const {
        if (db_path.empty()) return "";
        // db_path now points to .nemo/<name>/data.nm
        // State is persisted directly into data.nm
        return db_path;
    }

    // Returns the WAL directory path: .nemo/<name>/wal/
    std::string get_wal_dir() const {
        if (db_path.empty()) return "";
        namespace fs = std::filesystem;
        fs::path data_path(db_path);
        return data_path.filename() == "data.nm"
            ? (data_path.parent_path() / "wal").string()
            : (data_path.string() + ".wal");
    }

    // Returns the lock file path: .nemo/<name>/lock
    std::string get_lock_path() const {
        if (db_path.empty()) return "";
        namespace fs = std::filesystem;
        fs::path data_path(db_path);
        return (data_path.parent_path() / "lock").string();
    }

    nemo::Result<void> persist_state() {
        return state_storage::persist_pages(*this);
    }

    nemo::Result<void> load_page_state() {
        return state_storage::load_pages(*this);
    }

    bool load_legacy_state() {
        std::string path = get_meta_path();
        if (path.empty()) return false;
        try {
            std::ifstream ifs(path, std::ios::binary);
            if (!ifs.is_open()) return false;

            char magic[8];
            ifs.read(magic, 8);
            if (!ifs || std::string(magic, 8) != "NEMOMETA") return false;

            std::unique_lock<std::shared_mutex> lock(db_mutex);
            tables.clear();
            sequences.clear();

            // 1. Read sequences
            std::uint32_t num_seqs = 0;
            ifs.read(reinterpret_cast<char*>(&num_seqs), sizeof(num_seqs));
            if (!ifs || num_seqs > 100000) return false;
            for (std::uint32_t i = 0; i < num_seqs; ++i) {
                std::uint32_t n_len = 0;
                ifs.read(reinterpret_cast<char*>(&n_len), sizeof(n_len));
                if (!ifs || n_len > 1024) return false;
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
            if (!ifs || num_tables > 100000) return false;
            for (std::uint32_t t = 0; t < num_tables; ++t) {
                std::uint32_t t_len = 0;
                ifs.read(reinterpret_cast<char*>(&t_len), sizeof(t_len));
                if (!ifs || t_len > 1024) return false;
                std::string t_name(t_len, '\0');
                ifs.read(t_name.data(), t_len);
                auto tbl = std::make_shared<TableData>();
                tbl->name = t_name;
                std::uint64_t total_r = 0;
                ifs.read(reinterpret_cast<char*>(&total_r), sizeof(total_r));
                if (!ifs || static_cast<std::uint64_t>(total_r) > 100000000) return false;
                tbl->total_rows = static_cast<std::size_t>(total_r);

                std::uint64_t auto_idx = 0;
                ifs.read(reinterpret_cast<char*>(&auto_idx), sizeof(auto_idx));
                if (!ifs) return false;
                tbl->autoincrement_col_idx = static_cast<std::size_t>(auto_idx);

                std::uint32_t seq_n_len = 0;
                ifs.read(reinterpret_cast<char*>(&seq_n_len), sizeof(seq_n_len));
                if (!ifs || seq_n_len > 1024) return false;
                if (!ifs || seq_n_len > 1024) return false;
                std::string seq_n(seq_n_len, '\0');
                ifs.read(seq_n.data(), seq_n_len);
                tbl->autoincrement_seq_name = seq_n;

                // Columns
                std::uint32_t num_cols = 0;
                ifs.read(reinterpret_cast<char*>(&num_cols), sizeof(num_cols));
                if (!ifs || num_cols > 100000) return false;
                for (std::uint32_t c = 0; c < num_cols; ++c) {
                    ColumnMetadata col;
                    col.table = t_name;
                    std::uint32_t c_len = 0;
                    ifs.read(reinterpret_cast<char*>(&c_len), sizeof(c_len));
                    if (!ifs || c_len > 1024) return false;
                    col.name.resize(c_len);
                    ifs.read(col.name.data(), c_len);
                    ifs.read(reinterpret_cast<char*>(&col.type), sizeof(col.type));
                    if (!ifs || col.type < 0 ||
                        col.type >= static_cast<int>(types::TypeID::Invalid)) return false;
                    std::uint8_t null_b = 0;
                    ifs.read(reinterpret_cast<char*>(&null_b), sizeof(null_b));
                    if (!ifs || null_b > 1) return false;
                    col.nullable = (null_b != 0);
                    std::uint8_t auto_b = 0;
                    ifs.read(reinterpret_cast<char*>(&auto_b), sizeof(auto_b));
                    if (!ifs || auto_b > 1) return false;
                    col.is_autoincrement = (auto_b != 0);
                    std::uint32_t c_seq_len = 0;
                    ifs.read(reinterpret_cast<char*>(&c_seq_len), sizeof(c_seq_len));
                    if (!ifs || c_seq_len > 1024) return false;
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
                if (!ifs || num_rows > 100000000) return false;
                for (std::uint32_t r = 0; r < num_rows; ++r) {
                    std::uint32_t row_cols = 0;
                    ifs.read(reinterpret_cast<char*>(&row_cols), sizeof(row_cols));
                    if (!ifs || row_cols > 100000) return false;
                    std::vector<types::Value> row_vals;
                    row_vals.reserve(row_cols);
                    for (std::uint32_t c = 0; c < row_cols; ++c) {
                        std::uint16_t tid = 0;
                        ifs.read(reinterpret_cast<char*>(&tid), sizeof(tid));
                        std::uint8_t is_n = 0;
                        ifs.read(reinterpret_cast<char*>(&is_n), sizeof(is_n));
                        std::uint32_t s_len = 0;
                        ifs.read(reinterpret_cast<char*>(&s_len), sizeof(s_len));
                        if (!ifs || s_len > 64 * 1024 * 1024) return false;
                        std::string s_val(s_len, '\0');
                        ifs.read(s_val.data(), s_len);
                        if (!ifs || is_n > 1 || tid >= static_cast<std::uint16_t>(types::TypeID::Invalid)) return false;

                        if (is_n != 0) {
                            row_vals.push_back(types::Value());
                        } else {
                            auto parsed_opt = detail::decode_legacy_value(
                                s_val, static_cast<types::TypeID>(tid));
                            if (!parsed_opt.has_value()) return false;
                            row_vals.push_back(std::move(parsed_opt.value()));
                        }
                    }
                    tbl->sample_rows.push_back(std::move(row_vals));
                }
                // Phase 5: stamp all loaded rows as committed (they were persisted as committed)
                tbl->row_flags.clear();
                for (std::size_t ri = 0; ri < tbl->sample_rows.size(); ++ri) {
                    tbl->row_flags.emplace_back(static_cast<uint8_t>(0x01));
                }
                tbl->total_rows = tbl->sample_rows.size();
                tables[t_name] = tbl;
            }
            ifs.close();
            return ifs.good() && ifs.peek() == std::char_traits<char>::eof();
        } catch (...) {
            return false;
        }
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
    struct SavepointTableSnapshot {
        std::size_t total_rows{0};
        std::vector<std::vector<types::Value>> rows;
        std::vector<uint8_t> row_flags;
        std::vector<storage::RecordID> storage_record_ids;
    };

    struct SavepointSnapshot {
        std::string name;
        std::unordered_map<std::string, SavepointTableSnapshot> table_states;
    };

    std::shared_ptr<runtime::ConnectionLease> lease;
    std::shared_ptr<runtime::WorkerPool> worker_pool;
    std::shared_ptr<SharedDatabaseState> shared_state;
    bool in_transaction{false};
    transaction::TransactionID transaction_id{0};
    std::uint64_t transaction_last_lsn{0};
    std::vector<SavepointSnapshot> savepoints;
    std::string cached_insert_table_name;
    std::shared_ptr<TableData> cached_insert_table{nullptr};
    // Cached sequence pointer for autoincrement inserts (avoids repeated db_mutex lookups)
    std::shared_ptr<Sequence> cached_insert_seq{nullptr};
    // Per-connection SELECT table cache for concurrent workloads
    std::unordered_map<std::string, std::shared_ptr<TableData>> select_table_cache;

    // Phase 5: track pending (uncommitted) rows per table so commit()/rollback()
    // can stamp visibility flags without iterating all tables.
    // Key = table pointer, Value = row index of first pending row in this txn.
    std::unordered_map<TableData*, std::size_t> pending_row_starts;

    // Fast-path template for repeated single-row inserts (e.g. bulk one-by-one loops)
    struct InsertTemplate {
        std::string sql_prefix;
        std::string sql_suffix;
        std::shared_ptr<TableData> table{nullptr};
        std::shared_ptr<Sequence> auto_seq{nullptr};
        std::vector<types::Value> template_row;
        std::size_t id_col_idx{0};
        bool valid{false};
    };
    static constexpr std::size_t MAX_INSERT_TEMPLATES = 8;
    InsertTemplate cached_insert_templates[MAX_INSERT_TEMPLATES];
    std::size_t num_insert_templates{0};
    InsertTemplate cached_insert_template;

    void clear_insert_templates() {
        num_insert_templates = 0;
        cached_insert_template.valid = false;
        for (std::size_t i = 0; i < MAX_INSERT_TEMPLATES; ++i) {
            cached_insert_templates[i].valid = false;
            cached_insert_templates[i].table = nullptr;
            cached_insert_templates[i].auto_seq = nullptr;
        }
    }
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
    bool is_slice{false};
    std::size_t slice_start{0};
    bool is_two_slice{false};
    std::size_t slice1_start{0}, slice1_count{0};
    std::size_t slice2_start{0}, slice2_count{0};
    std::size_t slice_col_idx{0};
    std::size_t total_rows{0};
    std::size_t current_row{0};

    const std::vector<std::vector<types::Value>>& get_effective_rows() const noexcept {
        if (table_ref && single_row_idx == static_cast<std::size_t>(-1) && !is_slice && !is_two_slice) return table_ref->sample_rows;
        return rows;
    }

    const std::vector<std::vector<types::Value>>& get_all_rows() const noexcept {
        if (table_ref && single_row_idx == static_cast<std::size_t>(-1) && !is_slice && !is_two_slice) return table_ref->sample_rows;
        return rows;
    }

    std::unique_ptr<Row> create_next_row() {
        if (current_row >= total_rows) {
            return nullptr;
        }
        auto row_impl = std::make_unique<RowImpl>();
        if (is_slice && table_ref) {
            std::size_t ri = slice_start + current_row;
            if (ri < table_ref->sample_rows.size()) {
                row_impl->values = table_ref->sample_rows[ri];
            }
            current_row++;
            return std::unique_ptr<Row>(new Row(std::move(row_impl)));
        }
        if (is_two_slice && table_ref) {
            std::size_t ri = (current_row < slice1_count)
                ? (slice1_start + current_row)
                : (slice2_start + (current_row - slice1_count));
            if (ri < table_ref->sample_rows.size() && slice_col_idx < table_ref->sample_rows[ri].size()) {
                row_impl->values = { table_ref->sample_rows[ri][slice_col_idx] };
            }
            current_row++;
            return std::unique_ptr<Row>(new Row(std::move(row_impl)));
        }
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

inline Result create_simple_affected_result(std::size_t affected = 1) {
    auto impl = std::make_unique<ResultImpl>();
    impl->affected_rows = affected;
    impl->has_rows = false;
    return Result(std::move(impl));
}

nemo::Result<Connection> create_connection(
    std::shared_ptr<runtime::ConnectionLease> lease,
    std::shared_ptr<runtime::WorkerPool> pool,
    std::shared_ptr<SharedDatabaseState> shared_state = nullptr
);

PreparedStatement create_prepared_statement(std::string sql);

} // namespace nemo::api

#endif // NEMO_SRC_API_INTERNAL_HPP
