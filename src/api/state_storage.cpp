#include "api_internal.hpp"

#include "nemo/storage/page_header.hpp"
#include "nemo/storage/page_handle.hpp"
#include "nemo/storage/serialization.hpp"
#include "nemo/storage/record_serializer.hpp"
#include "nemo/storage/record_deserializer.hpp"
#include "nemo/storage/table_page.hpp"
#include "nemo/catalog/catalog_serializer.hpp"
#include "nemo/catalog/schema.hpp"
#include "nemo/catalog/table_metadata.hpp"
#include "nemo/types/type_serializer.hpp"
#include "nemo/logging/logger.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace nemo::api::state_storage {
namespace {

constexpr std::array<std::byte, 8> STATE_MAGIC{
    std::byte{0x4E}, std::byte{0x45}, std::byte{0x4D}, std::byte{0x4F},
    std::byte{0x53}, std::byte{0x54}, std::byte{0x41}, std::byte{0x31}};
constexpr std::array<std::byte, 4> PAGE_MAGIC{
    std::byte{0x4E}, std::byte{0x53}, std::byte{0x54}, std::byte{0x31}};
constexpr std::uint32_t FORMAT_VERSION = 2;
constexpr std::size_t PAGE_LINK_SIZE = 24;
constexpr std::uint32_t MAX_ITEMS = 10'000'000;
constexpr std::uint32_t MAX_FIELD_SIZE = 64 * 1024 * 1024;
constexpr std::uint64_t MAX_STATE_SIZE = 16ULL * 1024ULL * 1024ULL * 1024ULL;

class Writer {
public:
    void u8(std::uint8_t value) {
        m_bytes.push_back(static_cast<std::byte>(value));
    }

    void u16(std::uint16_t value) {
        const auto offset = m_bytes.size();
        m_bytes.resize(offset + 2);
        storage::write_u16_le(m_bytes, offset, value);
    }

    void u32(std::uint32_t value) {
        const auto offset = m_bytes.size();
        m_bytes.resize(offset + 4);
        storage::write_u32_le(m_bytes, offset, value);
    }

    void u64(std::uint64_t value) {
        const auto offset = m_bytes.size();
        m_bytes.resize(offset + 8);
        storage::write_u64_le(m_bytes, offset, value);
    }

    void i64(std::int64_t value) {
        u64(static_cast<std::uint64_t>(value));
    }

    void record_id(const storage::RecordID& value) {
        u64(value.page_id().value());
        u32(value.slot_id());
    }

    void string(const std::string& value) {
        if (value.size() > MAX_FIELD_SIZE) {
            throw std::length_error("State string exceeds format limit");
        }
        u32(static_cast<std::uint32_t>(value.size()));
        bytes(std::as_bytes(std::span(value.data(), value.size())));
    }

    void value(const types::Value& value) {
        u16(static_cast<std::uint16_t>(value.type()));
        u8(value.is_null() ? 1 : 0);
        const auto encoded = value.is_null()
            ? std::vector<std::uint8_t>{}
            : types::TypeSerializer::serialize(value);
        if (encoded.size() > MAX_FIELD_SIZE) {
            throw std::length_error("State value exceeds format limit");
        }
        u32(static_cast<std::uint32_t>(encoded.size()));
        bytes(std::as_bytes(std::span(encoded.data(), encoded.size())));
    }

    void bytes(std::span<const std::byte> bytes) {
        if (bytes.size() > MAX_STATE_SIZE - m_bytes.size()) {
            throw std::length_error("State exceeds format limit");
        }
        m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
    }

    const std::vector<std::byte>& data() const noexcept {
        return m_bytes;
    }

private:
    std::vector<std::byte> m_bytes;
};

class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) : m_bytes(bytes) {}

    std::uint8_t u8() {
        require(1);
        return static_cast<std::uint8_t>(m_bytes[m_offset++]);
    }

    std::uint16_t u16() {
        require(2);
        const auto value = storage::read_u16_le(m_bytes, m_offset);
        m_offset += 2;
        return value;
    }

    std::uint32_t u32() {
        require(4);
        const auto value = storage::read_u32_le(m_bytes, m_offset);
        m_offset += 4;
        return value;
    }

    std::uint64_t u64() {
        require(8);
        const auto value = storage::read_u64_le(m_bytes, m_offset);
        m_offset += 8;
        return value;
    }

    std::int64_t i64() {
        return static_cast<std::int64_t>(u64());
    }

    storage::RecordID record_id() {
        const PageID page_id(u64());
        const auto slot_id = u32();
        if (!page_id.is_valid() || slot_id == storage::RecordID::INVALID_SLOT) {
            throw std::runtime_error("Invalid record identifier in database state");
        }
        return storage::RecordID(page_id, slot_id);
    }

    std::string string() {
        const auto size = u32();
        if (size > MAX_FIELD_SIZE) throw std::runtime_error("Invalid state string size");
        require(size);
        std::string result(reinterpret_cast<const char*>(m_bytes.data() + m_offset), size);
        m_offset += size;
        return result;
    }

    types::Value value() {
        const auto type_raw = u16();
        const auto null_marker = u8();
        const auto size = u32();
        if (null_marker > 1 || size > MAX_FIELD_SIZE) {
            throw std::runtime_error("Invalid state value header");
        }
        const auto type = static_cast<types::TypeID>(type_raw);
        if (type >= types::TypeID::Invalid) {
            throw std::runtime_error("Invalid type in database state");
        }
        require(size);
        if (null_marker != 0) {
            if (size != 0) throw std::runtime_error("Null state value has payload");
            return types::Value(type);
        }
        std::vector<std::uint8_t> encoded(size);
        if (size != 0) {
            std::memcpy(encoded.data(), m_bytes.data() + m_offset, size);
        }
        m_offset += size;
        return types::TypeSerializer::deserialize(encoded, type);
    }

    bool empty() const noexcept {
        return m_offset == m_bytes.size();
    }

private:
    void require(std::size_t size) const {
        if (size > m_bytes.size() - m_offset) {
            throw std::runtime_error("Truncated database state");
        }
    }

    std::span<const std::byte> m_bytes;
    std::size_t m_offset{0};
};

template <typename Map>
std::vector<std::string> sorted_string_keys(const Map& map) {
    std::vector<std::string> keys;
    keys.reserve(map.size());
    for (const auto& entry : map) keys.push_back(entry.first);
    std::sort(keys.begin(), keys.end());
    return keys;
}

bool serialize_state(const SharedDatabaseState& state, Writer& writer) {
    std::shared_lock db_lock(state.db_mutex);
    writer.bytes(STATE_MAGIC);
    writer.u32(FORMAT_VERSION);

    if (state.sequences.size() > MAX_ITEMS || state.tables.size() > MAX_ITEMS ||
        state.views.size() > MAX_ITEMS || state.indexes.size() > MAX_ITEMS) {
        return false;
    }

    writer.u32(static_cast<std::uint32_t>(state.sequences.size()));
    for (const auto& name : sorted_string_keys(state.sequences)) {
        const auto& sequence = state.sequences.at(name);
        std::lock_guard seq_lock(sequence->mutex);
        writer.string(sequence->name);
        writer.i64(sequence->current_val);
        writer.i64(sequence->start_with);
        writer.i64(sequence->increment_by);
        writer.i64(sequence->min_val);
        writer.i64(sequence->max_val);
        writer.u8(sequence->is_cycle ? 1 : 0);
        writer.u8(sequence->is_called ? 1 : 0);
    }

    writer.u32(static_cast<std::uint32_t>(state.views.size()));
    for (const auto& name : sorted_string_keys(state.views)) {
        writer.string(name);
        writer.string(state.views.at(name));
    }

    writer.u32(static_cast<std::uint32_t>(state.tables.size()));
    for (const auto& name : sorted_string_keys(state.tables)) {
        const auto& table = state.tables.at(name);
        std::shared_lock table_lock(table->mutex);
        writer.string(table->name);
        writer.u64(table->autoincrement_col_idx);
        writer.string(table->autoincrement_seq_name);
        if (table->columns.size() > MAX_ITEMS) return false;
        writer.u32(static_cast<std::uint32_t>(table->columns.size()));
        for (const auto& column : table->columns) {
            writer.string(column.name);
            writer.u32(static_cast<std::uint32_t>(column.type));
            writer.u8(column.nullable ? 1 : 0);
            writer.string(column.table);
            writer.u8(column.is_autoincrement ? 1 : 0);
            writer.string(column.sequence_name);
        }

        std::uint32_t visible_rows = 0;
        for (std::size_t row = 0; row < table->sample_rows.size(); ++row) {
            if (table->is_row_visible(row)) {
                if (visible_rows == MAX_ITEMS) return false;
                ++visible_rows;
            }
        }
        if (table->storage_record_ids.size() != table->sample_rows.size()) return false;
        writer.u64(table->storage_root_page.value());
        writer.u32(visible_rows);
        for (std::size_t row = 0; row < table->sample_rows.size(); ++row) {
            if (!table->is_row_visible(row)) continue;
            const auto& record_id = table->storage_record_ids[row];
            if (!record_id.is_valid()) return false;
            writer.record_id(record_id);
        }
    }

    writer.u32(static_cast<std::uint32_t>(state.indexes.size()));
    for (const auto& name : sorted_string_keys(state.indexes)) {
        const auto& index = state.indexes.at(name);
        writer.string(index->index_name);
        writer.string(index->table_name);
        writer.string(index->column_name);
        writer.u8(index->is_unique ? 1 : 0);
    }
    return true;
}

bool load_state_bytes(
    std::span<const std::byte> bytes,
    std::unordered_map<std::string, std::shared_ptr<Sequence>>& sequences,
    std::unordered_map<std::string, std::shared_ptr<TableData>>& tables,
    std::unordered_map<std::string, std::string>& views,
    std::unordered_map<std::string, std::shared_ptr<InMemoryIndex>>& indexes) {
    if (bytes.size() < STATE_MAGIC.size() ||
        !std::equal(STATE_MAGIC.begin(), STATE_MAGIC.end(), bytes.begin())) {
        return false;
    }
    Reader reader(bytes.subspan(STATE_MAGIC.size()));
    const auto version = reader.u32();
    if (version != 1 && version != FORMAT_VERSION) return false;

    const auto sequence_count = reader.u32();
    if (sequence_count > MAX_ITEMS) return false;
    sequences.clear();
    for (std::uint32_t i = 0; i < sequence_count; ++i) {
        auto sequence = std::make_shared<Sequence>();
        sequence->name = reader.string();
        sequence->current_val = reader.i64();
        sequence->start_with = reader.i64();
        sequence->increment_by = reader.i64();
        sequence->min_val = reader.i64();
        sequence->max_val = reader.i64();
        const auto cycle = reader.u8();
        const auto called = reader.u8();
        if (cycle > 1 || called > 1) return false;
        sequence->is_cycle = cycle != 0;
        sequence->is_called = called != 0;
        if (!sequences.emplace(sequence->name, std::move(sequence)).second) return false;
    }

    const auto view_count = reader.u32();
    if (view_count > MAX_ITEMS) return false;
    views.clear();
    for (std::uint32_t i = 0; i < view_count; ++i) {
        auto name = reader.string();
        auto sql = reader.string();
        if (!views.emplace(std::move(name), std::move(sql)).second) return false;
    }

    const auto table_count = reader.u32();
    if (table_count > MAX_ITEMS) return false;
    tables.clear();
    for (std::uint32_t i = 0; i < table_count; ++i) {
        auto table = std::make_shared<TableData>();
        table->name = reader.string();
        const auto auto_idx = reader.u64();
        if (auto_idx > std::numeric_limits<std::size_t>::max()) return false;
        table->autoincrement_col_idx = static_cast<std::size_t>(auto_idx);
        table->autoincrement_seq_name = reader.string();
        const auto column_count = reader.u32();
        if (column_count > MAX_ITEMS) return false;
        table->columns.reserve(column_count);
        for (std::uint32_t column_idx = 0; column_idx < column_count; ++column_idx) {
            ColumnMetadata column;
            column.name = reader.string();
            const auto type = reader.u32();
            if (type > static_cast<std::uint32_t>(types::TypeID::Invalid)) return false;
            column.type = static_cast<int>(type);
            const auto nullable = reader.u8();
            if (nullable > 1) return false;
            column.nullable = nullable != 0;
            column.table = reader.string();
            const auto autoincrement = reader.u8();
            if (autoincrement > 1) return false;
            column.is_autoincrement = autoincrement != 0;
            column.sequence_name = reader.string();
            table->column_indices[column.name] = column_idx;
            std::string lower;
            lower.reserve(column.name.size());
            for (const auto ch : column.name) {
                lower.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(ch))));
            }
            table->column_indices[lower] = column_idx;
            table->columns.push_back(std::move(column));
        }

        const auto row_count = version == 1 ? reader.u32() : 0;
        if (row_count > MAX_ITEMS) return false;
        if (version == 1) {
            table->sample_rows.reserve(row_count);
            table->row_flags.reserve(row_count);
            for (std::uint32_t row_idx = 0; row_idx < row_count; ++row_idx) {
                const auto value_count = reader.u32();
                if (value_count > MAX_ITEMS) return false;
                std::vector<types::Value> row;
                row.reserve(value_count);
                for (std::uint32_t value_idx = 0; value_idx < value_count; ++value_idx) {
                    row.push_back(reader.value());
                }
                table->sample_rows.push_back(std::move(row));
                table->row_flags.push_back(0x01);
            }
        } else {
            table->storage_root_page = PageID(reader.u64());
            const auto record_count = reader.u32();
            if (record_count > MAX_ITEMS) return false;
            table->storage_record_ids.reserve(record_count);
            for (std::uint32_t row_idx = 0; row_idx < record_count; ++row_idx) {
                table->storage_record_ids.push_back(reader.record_id());
            }
            if ((record_count == 0) != (table->storage_root_page.value() == 0)) return false;
            table->total_rows = record_count;
        }
        if (version == 1) table->total_rows = table->sample_rows.size();
        if (!tables.emplace(table->name, std::move(table)).second) return false;
    }

    const auto index_count = reader.u32();
    if (index_count > MAX_ITEMS) return false;
    indexes.clear();
    for (std::uint32_t i = 0; i < index_count; ++i) {
        auto index = std::make_shared<InMemoryIndex>();
        index->index_name = reader.string();
        index->table_name = reader.string();
        index->column_name = reader.string();
        const auto unique = reader.u8();
        if (unique > 1) return false;
        index->is_unique = unique != 0;
        const auto table_it = tables.find(index->table_name);
        if (table_it == tables.end()) return false;
        index->col_idx = table_it->second->get_col_idx(index->column_name);
        if (index->col_idx == static_cast<std::size_t>(-1)) return false;
        if (version == 1) {
            std::shared_lock table_lock(table_it->second->mutex);
            index->rebuild(*table_it->second);
        }
        if (!indexes.emplace(index->index_name, std::move(index)).second) return false;
    }
    return reader.empty();
}

bool read_chain(storage::BufferPool* pool, PageID root, std::vector<PageID>& pages) {
    std::unordered_set<std::uint64_t> seen;
    for (PageID page_id = root; page_id.is_valid();) {
        if (!seen.insert(page_id.value()).second) return false;
        auto handle = pool->fetch_page(page_id);
        if (!handle) return false;
        const auto header = handle.value().get().read_header();
        if (header.page_id != page_id || header.page_type != storage::PageType::Metadata ||
            header.payload_size < PAGE_LINK_SIZE ||
            header.payload_size > handle.value().get().payload_span().size()) {
            return false;
        }
        const auto payload = handle.value().get().payload_span();
        if (!std::equal(PAGE_MAGIC.begin(), PAGE_MAGIC.end(), payload.begin())) return false;
        const auto chunk_size = storage::read_u32_le(payload, 4);
        if (chunk_size > header.payload_size - PAGE_LINK_SIZE) return false;
        pages.push_back(page_id);
        page_id = PageID(storage::read_u64_le(payload, 8));
    }
    return !pages.empty();
}

bool collect_table_pages(storage::BufferPool* pool, PageID root,
                         std::vector<PageID>& pages) {
    std::unordered_set<std::uint64_t> seen;
    for (PageID page_id = root; page_id.is_valid();) {
        if (page_id.value() == 0 || !seen.insert(page_id.value()).second) return false;
        auto handle = pool->fetch_page(page_id);
        if (!handle) return false;
        storage::TablePage page(std::move(handle.value()));
        if (page.page_id() != page_id) return false;
        pages.push_back(page_id);
        page_id = page.next_page_id();
    }
    return true;
}

nemo::Result<std::vector<PageID>> persist_table_rows(
    SharedDatabaseState& state, storage::BufferPool* pool) {
    std::vector<PageID> old_pages;
    std::shared_lock db_lock(state.db_mutex);
    for (const auto& name : sorted_string_keys(state.tables)) {
        const auto& table = state.tables.at(name);
        std::unique_lock table_lock(table->mutex);
        std::vector<PageID> previous_pages;
        if (table->storage_root_page.value() != 0 &&
            table->storage_root_page.is_valid() &&
            !collect_table_pages(pool, table->storage_root_page, previous_pages)) {
            return nemo::Status::Corruption;
        }
        old_pages.insert(old_pages.end(), previous_pages.begin(), previous_pages.end());

        storage::TupleLayout layout;
        for (const auto& column : table->columns) {
            const auto type = static_cast<types::TypeID>(column.type);
            if (type >= types::TypeID::Invalid) return nemo::Status::Corruption;
            layout.add_column(type);
        }

        PageID root(0);
        PageID current{};
        std::vector<storage::RecordID> record_ids(table->sample_rows.size());
        for (std::size_t row_index = 0; row_index < table->sample_rows.size(); ++row_index) {
            if (!table->is_row_visible(row_index)) continue;
            const auto& values = table->sample_rows[row_index];
            if (values.size() != table->columns.size()) return nemo::Status::Corruption;
            std::vector<types::Value> normalized_values;
            normalized_values.reserve(values.size());
            for (std::size_t column_index = 0; column_index < values.size(); ++column_index) {
                const auto type = static_cast<types::TypeID>(
                    table->columns[column_index].type);
                if (values[column_index].is_null()) {
                    normalized_values.emplace_back(type);
                } else if (values[column_index].type() == type) {
                    normalized_values.push_back(values[column_index]);
                } else {
                    auto cast = types::TypeCast::cast(values[column_index], type);
                    if (!cast) return nemo::Status::InvalidArgument;
                    normalized_values.push_back(std::move(*cast));
                }
            }
            storage::Record record(std::move(normalized_values));
            const auto record_bytes = storage::RecordSerializer::serialize(record, layout);

            if (!current.is_valid()) {
                auto handle = pool->allocate_page();
                if (!handle) return handle.error();
                storage::TablePage page(std::move(handle.value()));
                page.init();
                root = page.page_id();
                current = root;
            }

            auto handle = pool->fetch_page(current);
            if (!handle) return handle.error();
            storage::TablePage page(std::move(handle.value()));
            auto inserted = page.insert(record_bytes);
            if (!inserted && inserted.error() == nemo::Status::InternalError) {
                auto compact_result = page.compact();
                if (!compact_result) return compact_result.error();
                inserted = page.insert(record_bytes);
            }
            if (!inserted && inserted.error() == nemo::Status::InternalError) {
                auto next_handle = pool->allocate_page();
                if (!next_handle) return next_handle.error();
                storage::TablePage next_page(std::move(next_handle.value()));
                next_page.init();
                next_page.set_prev_page_id(current);
                const PageID next_id = next_page.page_id();
                page.set_next_page_id(next_id);
                current = next_id;
                inserted = next_page.insert(record_bytes);
            }
            if (!inserted) return inserted.error();
            record_ids[row_index] = inserted.value();
        }

        table->storage_root_page = root;
        table->storage_record_ids = std::move(record_ids);
        table->page_cache_current = false;
    }
    return old_pages;
}

nemo::Result<std::vector<std::byte>> serialize_table_row(
    const TableData& table, const std::vector<types::Value>& values) {
    if (values.size() != table.columns.size()) {
        NEMO_LOG_ERROR("Storage", "SQL row width does not match table schema");
        return nemo::Status::InvalidArgument;
    }
    storage::TupleLayout layout;
    std::vector<types::Value> normalized_values;
    normalized_values.reserve(values.size());
    for (std::size_t column_index = 0; column_index < values.size(); ++column_index) {
        const auto type = static_cast<types::TypeID>(table.columns[column_index].type);
        if (type >= types::TypeID::Invalid) return nemo::Status::Corruption;
        layout.add_column(type);
        if (values[column_index].is_null()) {
            normalized_values.emplace_back(type);
        } else if (values[column_index].type() == type) {
            normalized_values.push_back(values[column_index]);
        } else {
            auto cast = types::TypeCast::cast(values[column_index], type);
            if (!cast) {
                NEMO_LOG_ERROR("Storage", "SQL row value cannot be cast to its column type");
                return nemo::Status::InvalidArgument;
            }
            normalized_values.push_back(std::move(*cast));
        }
    }
    try {
        return storage::RecordSerializer::serialize(
            storage::Record(std::move(normalized_values)), layout);
    } catch (const std::exception& error) {
        NEMO_LOG_ERROR("Storage", error.what());
        return nemo::Status::InvalidArgument;
    }
}

nemo::Result<storage::RecordID> append_table_record(
    SharedDatabaseState& state, TableData& table,
    const std::vector<std::byte>& record_bytes) {
    auto* pool = state.buffer_pool;
    if (pool == nullptr) return storage::RecordID{};

    PageID current = table.storage_root_page;
    if (!current.is_valid() || current.value() == 0) {
        auto allocated = pool->allocate_page();
        if (!allocated) return allocated.error();
        PageID root;
        {
            storage::TablePage page(std::move(allocated.value()));
            page.init();
            root = page.page_id();
            auto inserted = page.insert(record_bytes);
            if (!inserted) {
                NEMO_LOG_ERROR("Storage", "Unable to insert SQL row into a new table page");
                return inserted.error();
            }
            table.storage_root_page = root;
            return inserted.value();
        }
    }

    PageID last = current;
    while (current.is_valid()) {
        PageID next;
        {
            auto handle = pool->fetch_page(current);
            if (!handle) return handle.error();
            storage::TablePage page(std::move(handle.value()));
            auto inserted = page.insert(record_bytes);
            if (!inserted && inserted.error() == nemo::Status::InternalError) {
                auto compacted = page.compact();
                if (!compacted) return compacted.error();
                inserted = page.insert(record_bytes);
            }
            if (inserted) return inserted.value();
            if (inserted.error() != nemo::Status::InternalError) return inserted.error();
            next = page.next_page_id();
            last = current;
        }
        if (!next.is_valid()) break;
        current = next;
    }

    auto allocated = pool->allocate_page();
    if (!allocated) return allocated.error();
    PageID new_page_id;
    storage::RecordID new_record_id;
    {
        storage::TablePage page(std::move(allocated.value()));
        page.init();
        page.set_prev_page_id(last);
        new_page_id = page.page_id();
        auto inserted = page.insert(record_bytes);
        if (!inserted) return inserted.error();
        new_record_id = inserted.value();
    }
    auto last_handle = pool->fetch_page(last);
    if (!last_handle) return last_handle.error();
    storage::TablePage last_page(std::move(last_handle.value()));
    last_page.set_next_page_id(new_page_id);
    return new_record_id;
}

nemo::Result<void> erase_record(
    SharedDatabaseState& state, const storage::RecordID& record_id) {
    if (!record_id.is_valid() || state.buffer_pool == nullptr) return {};
    auto handle = state.buffer_pool->fetch_page(record_id.page_id());
    if (!handle) return handle.error();
    storage::TablePage page(std::move(handle.value()));
    return page.erase(record_id);
}

nemo::Result<void> persist_catalog(SharedDatabaseState& state,
                                   storage::BufferPool* pool) {
    auto root_page = state.disk_manager->get_database_header().catalog_root_page;
    if (root_page.value() == 0) {
        auto root = pool->allocate_page();
        if (!root) return root.error();
        root_page = root.value().get().id();
        state.disk_manager->set_catalog_root_page(root_page);
    }

    std::unordered_map<std::uint32_t, catalog::SchemaMetadata> schemas;
    std::unordered_map<std::uint32_t, catalog::TableMetadata> tables;
    std::unordered_map<std::uint32_t, catalog::IndexMetadata> indexes;
    std::unordered_map<std::string, std::uint32_t> table_ids;
    std::unordered_map<std::string, std::vector<std::uint32_t>> column_ids;
    std::uint32_t next_column = 1;
    std::uint32_t next_table = 1;
    std::uint32_t next_index = 1;

    catalog::SchemaMetadata main_schema;
    main_schema.schema_id = catalog::SchemaID(1);
    main_schema.name = "main";
    schemas.emplace(1, std::move(main_schema));

    {
        std::shared_lock db_lock(state.db_mutex);
        for (const auto& name : sorted_string_keys(state.tables)) {
            const auto& source = state.tables.at(name);
            std::shared_lock table_lock(source->mutex);
            catalog::TableMetadata table;
            table.table_id = catalog::TableID(next_table);
            table.schema_id = catalog::SchemaID(1);
            table.name = source->name;
            table.first_page_id = source->storage_root_page;
            auto& table_column_ids = column_ids[name];
            table_column_ids.reserve(source->columns.size());
            for (std::size_t i = 0; i < source->columns.size(); ++i) {
                const auto& column = source->columns[i];
                const auto type = static_cast<types::TypeID>(column.type);
                if (type >= types::TypeID::Invalid) return nemo::Status::Corruption;
                table_column_ids.push_back(next_column);
                table.schema.add_column(catalog::Column(
                    next_column++, column.name, type, static_cast<std::uint32_t>(i),
                    column.nullable));
            }
            table_ids.emplace(name, next_table);
            tables.emplace(next_table, std::move(table));
            ++next_table;
        }

        for (const auto& name : sorted_string_keys(state.indexes)) {
            const auto& source = state.indexes.at(name);
            const auto table_id = table_ids.find(source->table_name);
            const auto table = state.tables.find(source->table_name);
            if (table_id == table_ids.end() || table == state.tables.end() ||
                source->col_idx >= table->second->columns.size() ||
                source->col_idx >= column_ids[source->table_name].size()) {
                return nemo::Status::Corruption;
            }
            catalog::IndexMetadata index;
            index.index_id = nemo::IndexID(next_index);
            index.schema_id = catalog::SchemaID(1);
            index.table_id = nemo::TableID(table_id->second);
            index.name = source->index_name;
            index.indexed_columns.push_back(
                column_ids[source->table_name][source->col_idx]);
            index.is_unique = source->is_unique;
            indexes.emplace(next_index, std::move(index));
            ++next_index;
        }
    }

    return catalog::CatalogSerializer::save(
        pool, root_page, schemas, tables, indexes,
        2, next_table, next_column, next_index, 1);
}

} // namespace

nemo::Result<void> refresh_table_rows(SharedDatabaseState& state, TableData& table) {
    if (state.buffer_pool == nullptr) return {};
    {
        std::shared_lock table_lock(table.mutex);
        const bool cache_has_only_live_records =
            table.storage_record_ids.size() == table.sample_rows.size() &&
            table.row_flags.size() == table.sample_rows.size() &&
            std::all_of(table.storage_record_ids.begin(), table.storage_record_ids.end(),
                [](const storage::RecordID& record_id) { return record_id.is_valid(); }) &&
            std::all_of(table.row_flags.begin(), table.row_flags.end(),
                [](std::uint8_t flags) { return (flags & 0x03) == 0x01; });
        if (table.page_cache_current && cache_has_only_live_records) return {};
    }

    std::string table_name;
    table_name.reserve(table.name.size());
    for (const auto ch : table.name) {
        table_name.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    }

    bool rows_replaced = false;
    for (;;) {
        std::vector<std::vector<types::Value>> cached_rows;
        std::vector<std::uint8_t> cached_flags;
        std::vector<storage::RecordID> source_ids;
        std::vector<std::vector<types::Value>> loaded_rows;
        std::vector<storage::RecordID> loaded_ids;
        bool cache_matches = false;
        {
            std::shared_lock table_lock(table.mutex);
            cached_rows = table.sample_rows;
            cached_flags = table.row_flags;
            source_ids = table.storage_record_ids;

            storage::TupleLayout layout;
            for (const auto& column : table.columns) {
                const auto type = static_cast<types::TypeID>(column.type);
                if (type >= types::TypeID::Invalid) return nemo::Status::Corruption;
                layout.add_column(type);
            }
            loaded_rows.reserve(source_ids.size());
            loaded_ids.reserve(source_ids.size());
            for (const auto& record_id : source_ids) {
                if (!record_id.is_valid()) continue;
                auto handle = state.buffer_pool->fetch_page(record_id.page_id());
                if (!handle) return handle.error();
                if (handle.value().get().read_header().page_type != storage::PageType::Table) {
                    return nemo::Status::Corruption;
                }
                storage::TablePage page(std::move(handle.value()));
                auto record_bytes = page.get(record_id);
                if (!record_bytes) return nemo::Status::Corruption;
                storage::Record record;
                try {
                    record = storage::RecordDeserializer::deserialize(
                        record_bytes.value(), layout);
                } catch (const std::exception& error) {
                    NEMO_LOG_ERROR("Storage", error.what());
                    return nemo::Status::Corruption;
                }
                if (record.field_count() != table.columns.size()) {
                    return nemo::Status::Corruption;
                }
                loaded_rows.push_back(record.fields());
                loaded_ids.push_back(record_id);
            }

            const bool flags_are_visible =
                cached_flags.size() == cached_rows.size() &&
                std::all_of(cached_flags.begin(), cached_flags.end(),
                    [](std::uint8_t flags) { return (flags & 0x03) == 0x01; });
            if (loaded_rows == cached_rows && flags_are_visible &&
                source_ids.size() == cached_rows.size()) {
                cache_matches = true;
            }
        }

        {
            std::unique_lock table_lock(table.mutex);
            if (table.storage_record_ids != source_ids ||
                table.sample_rows != cached_rows ||
                table.row_flags != cached_flags) {
                continue;
            }
            if (cache_matches) {
                table.page_cache_current = true;
                return {};
            }
            table.sample_rows = std::move(loaded_rows);
            table.storage_record_ids = std::move(loaded_ids);
            table.row_flags.assign(table.sample_rows.size(), 0x01);
            table.total_rows = table.sample_rows.size();
            table.page_cache_current = true;
            rows_replaced = true;
        }
        break;
    }

    if (!rows_replaced) return {};
    std::vector<std::shared_ptr<InMemoryIndex>> table_indexes;
    {
        std::shared_lock db_lock(state.db_mutex);
        for (const auto& [_, index] : state.indexes) {
            if (index->table_name == table_name) table_indexes.push_back(index);
        }
    }
    {
        std::shared_lock table_lock(table.mutex);
        for (const auto& index : table_indexes) index->rebuild(table);
    }
    return {};
}

nemo::Result<std::optional<std::vector<types::Value>>> read_page_row(
    SharedDatabaseState& state, TableData& table, std::size_t row_index,
    std::int64_t expected_id) {
    if (state.buffer_pool == nullptr) return std::optional<std::vector<types::Value>>{};
    std::shared_lock table_lock(table.mutex);
    if (table.get_col_idx("id") != 0 ||
        row_index >= table.storage_record_ids.size() ||
        !table.is_row_visible(row_index)) {
        return std::optional<std::vector<types::Value>>{};
    }

    const auto record_id = table.storage_record_ids[row_index];
    if (!record_id.is_valid()) return std::optional<std::vector<types::Value>>{};

    storage::TupleLayout layout;
    for (const auto& column : table.columns) {
        const auto type = static_cast<types::TypeID>(column.type);
        if (type >= types::TypeID::Invalid) return nemo::Status::Corruption;
        layout.add_column(type);
    }
    auto handle = state.buffer_pool->fetch_page(record_id.page_id());
    if (!handle) return handle.error();
    if (handle.value().get().read_header().page_type != storage::PageType::Table) {
        return nemo::Status::Corruption;
    }
    storage::TablePage page(std::move(handle.value()));
    auto record_bytes = page.get(record_id);
    if (!record_bytes) return nemo::Status::Corruption;
    storage::Record record;
    try {
        record = storage::RecordDeserializer::deserialize(record_bytes.value(), layout);
    } catch (const std::exception& error) {
        NEMO_LOG_ERROR("Storage", error.what());
        return nemo::Status::Corruption;
    }
    if (record.field_count() != table.columns.size() || record.field_count() == 0) {
        return nemo::Status::Corruption;
    }
    auto id_value = types::TypeCast::cast(record.field(0), types::TypeID::BigInt);
    if (!id_value || id_value->is_null() ||
        id_value->get<std::int64_t>() != expected_id) {
        return std::optional<std::vector<types::Value>>{};
    }
    return std::optional<std::vector<types::Value>>(record.fields());
}

nemo::Result<storage::RecordID> insert_page_row(
    SharedDatabaseState& state, TableData& table,
    const std::vector<types::Value>& row) {
    if (state.buffer_pool == nullptr) return storage::RecordID{};
    auto encoded = serialize_table_row(table, row);
    if (!encoded) return encoded.error();
    return append_table_record(state, table, encoded.value());
}

nemo::Result<void> update_page_row(
    SharedDatabaseState& state, TableData& table, std::size_t row_index,
    const std::vector<types::Value>& row) {
    if (state.buffer_pool == nullptr) return {};
    auto encoded = serialize_table_row(table, row);
    if (!encoded) return encoded.error();
    if (table.storage_record_ids.size() <= row_index) {
        table.storage_record_ids.resize(row_index + 1);
    }
    auto& record_id = table.storage_record_ids[row_index];
    if (!record_id.is_valid()) {
        auto inserted = append_table_record(state, table, encoded.value());
        if (!inserted) return inserted.error();
        record_id = inserted.value();
        table.page_cache_current = true;
        return {};
    }

    nemo::Result<void> updated;
    {
        auto handle = state.buffer_pool->fetch_page(record_id.page_id());
        if (!handle) return handle.error();
        storage::TablePage page(std::move(handle.value()));
        updated = page.update(record_id, encoded.value());
    }
    if (updated) {
        table.page_cache_current = true;
        return {};
    }
    if (updated.error() != nemo::Status::InternalError) return updated.error();

    const auto old_record_id = record_id;
    auto inserted = append_table_record(state, table, encoded.value());
    if (!inserted) return inserted.error();
    auto erased = erase_record(state, old_record_id);
    if (!erased) return erased.error();
    record_id = inserted.value();
    table.page_cache_current = true;
    return {};
}

nemo::Result<void> erase_page_row(
    SharedDatabaseState& state, TableData& table, std::size_t row_index) {
    if (row_index >= table.storage_record_ids.size()) return {};
    auto& record_id = table.storage_record_ids[row_index];
    auto result = erase_record(state, record_id);
    if (!result) return result;
    record_id = storage::RecordID{};
    table.page_cache_current = true;
    return {};
}

nemo::Result<void> persist_pages(SharedDatabaseState& state) {
    if (state.buffer_pool == nullptr || state.disk_manager == nullptr) {
        return state.db_path.empty() ? nemo::Result<void>{}
                                     : nemo::Result<void>{nemo::Status::InvalidState};
    }
    try {
        auto& disk = *state.disk_manager;
        auto& pool = *state.buffer_pool;
        auto old_table_pages = persist_table_rows(state, &pool);
        if (!old_table_pages) return old_table_pages.error();
        auto catalog_result = persist_catalog(state, &pool);
        if (!catalog_result) return catalog_result;

        Writer writer;
        if (!serialize_state(state, writer) || writer.data().size() > MAX_STATE_SIZE) {
            return nemo::Status::InvalidArgument;
        }
        PageID root = disk.get_database_header().metadata_root_page;
        if (root.value() == 0) {
            auto allocated = pool.allocate_page();
            if (!allocated) return allocated.error();
            root = allocated.value().get().id();
            disk.set_metadata_root_page(root);
        }

        std::vector<PageID> pages;
        if (!read_chain(&pool, root, pages)) {
            auto root_handle = pool.fetch_page(root);
            if (!root_handle) return root_handle.error();
            const auto root_header = root_handle.value().get().read_header();
            if (root_header.page_type != storage::PageType::Invalid) {
                return nemo::Status::Corruption;
            }
            pages.push_back(root);
        }

        auto root_handle = pool.fetch_page(root);
        if (!root_handle) return root_handle.error();
        const auto payload_size = root_handle.value().get().payload_span().size();
        if (payload_size <= PAGE_LINK_SIZE) return nemo::Status::InvalidState;
        const auto chunk_capacity = payload_size - PAGE_LINK_SIZE;
        const auto& bytes = writer.data();
        const auto needed_pages = std::max<std::size_t>(
            1, (bytes.size() + chunk_capacity - 1) / chunk_capacity);
        while (pages.size() < needed_pages) {
            auto allocated = pool.allocate_page();
            if (!allocated) return allocated.error();
            pages.push_back(allocated.value().get().id());
        }

        std::size_t offset = 0;
        for (std::size_t i = 0; i < needed_pages; ++i) {
            auto handle = pool.fetch_page(pages[i]);
            if (!handle) return handle.error();
            auto& page = handle.value().get_mut();
            auto header = page.read_header();
            header.page_type = storage::PageType::Metadata;
            header.header_version = 1;
            auto payload = page.payload_span();
            const auto chunk_size = std::min(chunk_capacity, bytes.size() - offset);
            std::copy(PAGE_MAGIC.begin(), PAGE_MAGIC.end(), payload.begin());
            storage::write_u32_le(payload, 4, static_cast<std::uint32_t>(chunk_size));
            const PageID next = i + 1 < needed_pages ? pages[i + 1] : PageID{};
            storage::write_u64_le(payload, 8, next.value());
            storage::write_u64_le(payload, 16, static_cast<std::uint64_t>(bytes.size()));
            if (chunk_size != 0) {
                std::memcpy(payload.data() + PAGE_LINK_SIZE, bytes.data() + offset, chunk_size);
            }
            std::fill(payload.begin() + static_cast<std::ptrdiff_t>(PAGE_LINK_SIZE + chunk_size),
                      payload.end(), std::byte{0});
            offset += chunk_size;
            header.payload_size = static_cast<std::uint32_t>(PAGE_LINK_SIZE + chunk_size);
            page.write_header(header);
            handle.value().mark_dirty();
        }

        for (std::size_t i = needed_pages; i < pages.size(); ++i) {
            auto result = disk.deallocate_page(pages[i]);
            if (!result) return result;
        }
        auto flush_result = pool.flush_all();
        if (!flush_result) return flush_result;
        flush_result = disk.flush();
        if (!flush_result) return flush_result;
        for (const auto page_id : old_table_pages.value()) {
            auto deallocate_result = disk.deallocate_page(page_id);
            if (!deallocate_result) return deallocate_result;
        }
        return disk.flush();
    } catch (const std::exception& error) {
        NEMO_LOG_ERROR("Storage", error.what());
        return nemo::Status::InvalidArgument;
    }
}

nemo::Result<void> load_pages(SharedDatabaseState& state) {
    if (state.buffer_pool == nullptr || state.disk_manager == nullptr) {
        return nemo::Status::InvalidState;
    }
    const PageID root = state.disk_manager->get_database_header().metadata_root_page;
    if (root.value() == 0) return {};

    try {
        std::vector<PageID> pages;
        if (!read_chain(state.buffer_pool, root, pages)) {
            NEMO_LOG_ERROR("Storage", "Invalid SQL-state metadata page chain");
            return nemo::Status::Corruption;
        }
        std::vector<std::byte> bytes;
        std::uint64_t expected_size = 0;
        for (const auto page_id : pages) {
            auto handle = state.buffer_pool->fetch_page(page_id);
            if (!handle) return handle.error();
            const auto payload = handle.value().get().payload_span();
            const auto header = handle.value().get().read_header();
            const auto chunk_size = storage::read_u32_le(payload, 4);
            const auto total_size = storage::read_u64_le(payload, 16);
            if (total_size == 0 || total_size > MAX_STATE_SIZE ||
                chunk_size > header.payload_size - PAGE_LINK_SIZE ||
                (expected_size != 0 && total_size != expected_size) ||
                chunk_size > total_size - bytes.size()) {
                return nemo::Status::Corruption;
            }
            expected_size = total_size;
            bytes.insert(bytes.end(),
                payload.begin() + static_cast<std::ptrdiff_t>(PAGE_LINK_SIZE),
                payload.begin() + static_cast<std::ptrdiff_t>(PAGE_LINK_SIZE + chunk_size));
            if (bytes.size() == expected_size) break;
        }
        if (expected_size == 0 || bytes.size() != expected_size) return nemo::Status::Corruption;

        std::unordered_map<std::string, std::shared_ptr<Sequence>> sequences;
        std::unordered_map<std::string, std::shared_ptr<TableData>> tables;
        std::unordered_map<std::string, std::string> views;
        std::unordered_map<std::string, std::shared_ptr<InMemoryIndex>> indexes;
        if (!load_state_bytes(bytes, sequences, tables, views, indexes)) {
            NEMO_LOG_ERROR("Storage", "Invalid SQL-state metadata payload");
            return nemo::Status::Corruption;
        }

        const PageID catalog_root =
            state.disk_manager->get_database_header().catalog_root_page;
        if (catalog_root.value() != 0 && catalog_root.is_valid()) {
            std::unordered_map<std::uint32_t, catalog::SchemaMetadata> schemas;
            std::unordered_map<std::uint32_t, catalog::TableMetadata> catalog_tables;
            std::unordered_map<std::uint32_t, catalog::IndexMetadata> catalog_indexes;
            std::uint32_t next_schema = 0;
            std::uint32_t next_table = 0;
            std::uint32_t next_column = 0;
            std::uint32_t next_index = 0;
            std::uint32_t next_constraint = 0;
            auto catalog_result = catalog::CatalogSerializer::load(
                state.buffer_pool, catalog_root, schemas, catalog_tables, catalog_indexes,
                next_schema, next_table, next_column, next_index, next_constraint);
            if (!catalog_result) {
                NEMO_LOG_ERROR("Storage", "Catalog page chain could not be decoded");
                return nemo::Status::Corruption;
            }
            if (catalog_tables.size() != tables.size()) {
                NEMO_LOG_ERROR("Storage", "Catalog table count does not match SQL state");
                return nemo::Status::Corruption;
            }
            for (const auto& [_, catalog_table] : catalog_tables) {
                const auto table = tables.find(catalog_table.name);
                if (table == tables.end() ||
                    table->second->storage_root_page != catalog_table.first_page_id ||
                    table->second->columns.size() != catalog_table.schema.column_count()) {
                    NEMO_LOG_ERROR("Storage", "Catalog table metadata does not match SQL state");
                    return nemo::Status::Corruption;
                }
            }
        }

        for (const auto& [_, table] : tables) {
            storage::TupleLayout layout;
            for (const auto& column : table->columns) {
                const auto type = static_cast<types::TypeID>(column.type);
                if (type >= types::TypeID::Invalid) return nemo::Status::Corruption;
                layout.add_column(type);
            }
            for (const auto& record_id : table->storage_record_ids) {
                auto handle = state.buffer_pool->fetch_page(record_id.page_id());
                if (!handle) return handle.error();
                if (handle.value().get().read_header().page_type != storage::PageType::Table) {
                    NEMO_LOG_ERROR("Storage", "SQL row identifier refers to a non-table page");
                    return nemo::Status::Corruption;
                }
                storage::TablePage page(std::move(handle.value()));
                auto record_bytes = page.get(record_id);
                if (!record_bytes) {
                    NEMO_LOG_ERROR("Storage", "SQL row identifier is not present in its table page");
                    return nemo::Status::Corruption;
                }
                auto record = storage::RecordDeserializer::deserialize(
                    record_bytes.value(), layout);
                if (record.field_count() != table->columns.size()) {
                    return nemo::Status::Corruption;
                }
                table->sample_rows.push_back(record.fields());
                table->row_flags.push_back(0x01);
            }
            table->total_rows = table->sample_rows.size();
        }

        for (const auto& [_, index] : indexes) {
            const auto table = tables.find(index->table_name);
            if (table == tables.end()) return nemo::Status::Corruption;
            std::shared_lock table_lock(table->second->mutex);
            index->rebuild(*table->second);
        }

        std::unique_lock lock(state.db_mutex);
        state.sequences.swap(sequences);
        state.tables.swap(tables);
        state.views.swap(views);
        state.indexes.swap(indexes);
        state.secondary_indexes.clear();
        for (const auto& [name, _] : state.indexes) {
            state.secondary_indexes.insert(name);
        }
        return {};
    } catch (const std::exception& error) {
        NEMO_LOG_ERROR("Storage", error.what());
        return nemo::Status::Corruption;
    }
}

} // namespace nemo::api::state_storage
