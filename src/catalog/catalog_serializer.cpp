#include "nemo/catalog/catalog_serializer.hpp"

#include "nemo/catalog/column.hpp"
#include "nemo/storage/page_header.hpp"
#include "nemo/storage/page_handle.hpp"
#include "nemo/storage/serialization.hpp"
#include "nemo/types/type_serializer.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <unordered_set>

namespace nemo::catalog {
namespace {

constexpr std::array<std::byte, 4> PAGE_MAGIC{
    std::byte{0x4E}, std::byte{0x43}, std::byte{0x41}, std::byte{0x54}};
constexpr std::array<std::byte, 8> CATALOG_MAGIC{
    std::byte{0x4E}, std::byte{0x45}, std::byte{0x4D}, std::byte{0x4F},
    std::byte{0x43}, std::byte{0x41}, std::byte{0x54}, std::byte{0x31}};
constexpr std::uint32_t CATALOG_VERSION = 1;
constexpr std::size_t PAGE_LINK_HEADER_SIZE = 24;
constexpr std::uint32_t MAX_CATALOG_ITEMS = 1'000'000;
constexpr std::uint32_t MAX_FIELD_BYTES = 64 * 1024 * 1024;
constexpr std::uint64_t MAX_CATALOG_BYTES = 1024ULL * 1024ULL * 1024ULL;

class Writer {
public:
    void u8(std::uint8_t value) {
        m_bytes.push_back(static_cast<std::byte>(value));
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

    void bytes(std::span<const std::byte> value) {
        m_bytes.insert(m_bytes.end(), value.begin(), value.end());
    }

    void string(const std::string& value) {
        if (value.size() > MAX_FIELD_BYTES) {
            throw std::length_error("Catalog string exceeds format limit");
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
        if (encoded.size() > MAX_FIELD_BYTES) {
            throw std::length_error("Catalog value exceeds format limit");
        }
        u32(static_cast<std::uint32_t>(encoded.size()));
        bytes(std::as_bytes(std::span(encoded.data(), encoded.size())));
    }

    const std::vector<std::byte>& data() const noexcept {
        return m_bytes;
    }

private:
    void u16(std::uint16_t value) {
        const auto offset = m_bytes.size();
        m_bytes.resize(offset + 2);
        storage::write_u16_le(m_bytes, offset, value);
    }

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

    std::string string() {
        const std::uint32_t size = u32();
        if (size > MAX_FIELD_BYTES) {
            throw std::runtime_error("Catalog string exceeds format limit");
        }
        require(size);
        std::string result(reinterpret_cast<const char*>(m_bytes.data() + m_offset), size);
        m_offset += size;
        return result;
    }

    types::Value value() {
        const auto type = static_cast<types::TypeID>(u16());
        const auto is_null = u8();
        if (is_null > 1) {
            throw std::runtime_error("Invalid catalog null marker");
        }
        const std::uint32_t size = u32();
        if (size > MAX_FIELD_BYTES) {
            throw std::runtime_error("Catalog value exceeds format limit");
        }
        require(size);
        std::vector<std::uint8_t> encoded(size);
        if (size != 0) {
            std::memcpy(encoded.data(), m_bytes.data() + m_offset, size);
        }
        m_offset += size;
        return is_null ? types::Value(type) : types::TypeSerializer::deserialize(encoded, type);
    }

    bool empty() const noexcept {
        return m_offset == m_bytes.size();
    }

private:
    void require(std::size_t size) const {
        if (size > m_bytes.size() - m_offset) {
            throw std::runtime_error("Truncated catalog data");
        }
    }

    std::span<const std::byte> m_bytes;
    std::size_t m_offset{0};
};

void write_optional_u32(Writer& writer, const std::optional<std::uint32_t>& value) {
    writer.u8(value.has_value() ? 1 : 0);
    if (value) writer.u32(*value);
}

std::optional<std::uint32_t> read_optional_u32(Reader& reader) {
    const auto present = reader.u8();
    if (present > 1) throw std::runtime_error("Invalid optional-value marker");
    if (present == 0) return std::nullopt;
    return reader.u32();
}

void serialize_column(Writer& writer, const Column& column) {
    writer.u32(column.id());
    writer.string(column.name());
    writer.u32(static_cast<std::uint32_t>(column.type()));
    writer.u32(column.ordinal());
    writer.u8(column.is_nullable() ? 1 : 0);
    write_optional_u32(writer, column.parameters().length);
    write_optional_u32(writer, column.parameters().precision);
    write_optional_u32(writer, column.parameters().scale);
    writer.u8(column.default_value().has_value() ? 1 : 0);
    if (column.default_value().has_value()) {
        writer.value(column.default_value().value());
    }
}

Column deserialize_column(Reader& reader) {
    const ColumnID id(reader.u32());
    std::string name = reader.string();
    const auto type = static_cast<types::TypeID>(reader.u32());
    const std::uint32_t ordinal = reader.u32();
    const auto nullable = reader.u8();
    if (nullable > 1) throw std::runtime_error("Invalid nullable marker");

    Column column(id, std::move(name), type, ordinal, nullable != 0);
    TypeParameters params;
    params.length = read_optional_u32(reader);
    params.precision = read_optional_u32(reader);
    params.scale = read_optional_u32(reader);
    column.set_parameters(std::move(params));

    const auto has_default = reader.u8();
    if (has_default > 1) throw std::runtime_error("Invalid default-value marker");
    if (has_default != 0) {
        column.set_default_value(DefaultValue(reader.value()));
    }
    return column;
}

std::vector<std::uint32_t> sorted_keys(
    const std::unordered_map<std::uint32_t, SchemaMetadata>& values) {
    std::vector<std::uint32_t> keys;
    keys.reserve(values.size());
    for (const auto& [key, _] : values) keys.push_back(key);
    std::sort(keys.begin(), keys.end());
    return keys;
}

std::vector<std::uint32_t> sorted_table_keys(
    const std::unordered_map<std::uint32_t, TableMetadata>& values) {
    std::vector<std::uint32_t> keys;
    keys.reserve(values.size());
    for (const auto& [key, _] : values) keys.push_back(key);
    std::sort(keys.begin(), keys.end());
    return keys;
}

std::vector<std::uint32_t> sorted_index_keys(
    const std::unordered_map<std::uint32_t, IndexMetadata>& values) {
    std::vector<std::uint32_t> keys;
    keys.reserve(values.size());
    for (const auto& [key, _] : values) keys.push_back(key);
    std::sort(keys.begin(), keys.end());
    return keys;
}

bool read_page_chain(storage::BufferPool* buffer_pool, PageID root,
                     std::vector<PageID>& pages) {
    std::unordered_set<std::uint64_t> seen;
    PageID current = root;
    while (current.is_valid()) {
        if (!seen.insert(current.value()).second) return false;
        auto handle_result = buffer_pool->fetch_page(current);
        if (!handle_result) return false;
        const auto& page = handle_result.value().get();
        const auto page_header = page.read_header();
        if (page_header.page_type == storage::PageType::Invalid && pages.empty()) {
            if (page_header.page_id != current) return false;
            pages.push_back(current);
            return true;
        }
        if (page_header.page_id != current ||
            page_header.page_type != storage::PageType::Metadata) return false;
        const auto payload = page.payload_span();
        if (payload.size() < PAGE_LINK_HEADER_SIZE ||
            page_header.payload_size < PAGE_LINK_HEADER_SIZE ||
            page_header.payload_size > payload.size() ||
            !std::equal(PAGE_MAGIC.begin(), PAGE_MAGIC.end(), payload.begin())) {
            return false;
        }
        const auto chunk_size = storage::read_u32_le(payload, 4);
        const PageID next(storage::read_u64_le(payload, 8));
        if (chunk_size > payload.size() - PAGE_LINK_HEADER_SIZE) return false;
        pages.push_back(current);
        current = next;
    }
    return !pages.empty();
}

bool serialize_catalog(
    const std::unordered_map<std::uint32_t, SchemaMetadata>& schemas,
    const std::unordered_map<std::uint32_t, TableMetadata>& tables,
    const std::unordered_map<std::uint32_t, IndexMetadata>& indexes,
    std::uint32_t next_schema, std::uint32_t next_table,
    std::uint32_t next_col, std::uint32_t next_idx, std::uint32_t next_const,
    Writer& writer) {
    if (schemas.size() > MAX_CATALOG_ITEMS || tables.size() > MAX_CATALOG_ITEMS ||
        indexes.size() > MAX_CATALOG_ITEMS) {
        return false;
    }

    writer.bytes(CATALOG_MAGIC);
    writer.u32(CATALOG_VERSION);
    writer.u32(next_schema);
    writer.u32(next_table);
    writer.u32(next_col);
    writer.u32(next_idx);
    writer.u32(next_const);

    writer.u32(static_cast<std::uint32_t>(schemas.size()));
    for (const auto key : sorted_keys(schemas)) {
        const auto& schema = schemas.at(key);
        writer.u32(static_cast<std::uint32_t>(schema.schema_id.value()));
        writer.string(schema.name);
    }

    writer.u32(static_cast<std::uint32_t>(tables.size()));
    for (const auto key : sorted_table_keys(tables)) {
        const auto& table = tables.at(key);
        writer.u64(table.table_id.value());
        writer.u32(static_cast<std::uint32_t>(table.schema_id.value()));
        writer.string(table.name);
        writer.u32(table.version);
        writer.u64(table.first_page_id.value());
        if (table.schema.column_count() > MAX_CATALOG_ITEMS) return false;
        writer.u32(static_cast<std::uint32_t>(table.schema.column_count()));
        for (const auto& column : table.schema.columns()) {
            serialize_column(writer, column);
        }
    }

    writer.u32(static_cast<std::uint32_t>(indexes.size()));
    for (const auto key : sorted_index_keys(indexes)) {
        const auto& index = indexes.at(key);
        writer.u64(index.index_id.value());
        writer.u32(static_cast<std::uint32_t>(index.schema_id.value()));
        writer.u64(index.table_id.value());
        writer.string(index.name);
        if (index.indexed_columns.size() > MAX_CATALOG_ITEMS) return false;
        writer.u32(static_cast<std::uint32_t>(index.indexed_columns.size()));
        for (const auto column_id : index.indexed_columns) {
            writer.u32(column_id);
        }
        writer.u64(index.root_page_id.value());
        writer.u32(index.tree_height);
        writer.u8(index.is_unique ? 1 : 0);
        writer.u8(index.is_primary_key ? 1 : 0);
        writer.u32(static_cast<std::uint32_t>(index.type));
    }
    return true;
}

bool deserialize_catalog(
    std::span<const std::byte> bytes,
    std::unordered_map<std::uint32_t, SchemaMetadata>& schemas,
    std::unordered_map<std::uint32_t, TableMetadata>& tables,
    std::unordered_map<std::uint32_t, IndexMetadata>& indexes,
    std::uint32_t& next_schema, std::uint32_t& next_table,
    std::uint32_t& next_col, std::uint32_t& next_idx, std::uint32_t& next_const) {
    if (bytes.size() < CATALOG_MAGIC.size() ||
        !std::equal(CATALOG_MAGIC.begin(), CATALOG_MAGIC.end(), bytes.begin())) {
        return false;
    }
    Reader data(bytes.subspan(CATALOG_MAGIC.size()));
    if (data.u32() != CATALOG_VERSION) return false;
    next_schema = data.u32();
    next_table = data.u32();
    next_col = data.u32();
    next_idx = data.u32();
    next_const = data.u32();

    const auto schema_count = data.u32();
    if (schema_count > MAX_CATALOG_ITEMS) return false;
    schemas.clear();
    for (std::uint32_t i = 0; i < schema_count; ++i) {
        SchemaMetadata schema;
        schema.schema_id = SchemaID(data.u32());
        schema.name = data.string();
        if (!schemas.emplace(static_cast<std::uint32_t>(schema.schema_id.value()),
                             std::move(schema)).second) {
            return false;
        }
    }

    const auto table_count = data.u32();
    if (table_count > MAX_CATALOG_ITEMS) return false;
    tables.clear();
    for (std::uint32_t i = 0; i < table_count; ++i) {
        TableMetadata table;
        const auto table_id = data.u64();
        if (table_id > std::numeric_limits<std::uint32_t>::max()) return false;
        table.table_id = TableID(table_id);
        table.schema_id = SchemaID(data.u32());
        table.name = data.string();
        table.version = data.u32();
        table.first_page_id = PageID(data.u64());
        const auto column_count = data.u32();
        if (column_count > MAX_CATALOG_ITEMS) return false;
        for (std::uint32_t col = 0; col < column_count; ++col) {
            table.schema.add_column(deserialize_column(data));
        }
        if (!tables.emplace(static_cast<std::uint32_t>(table.table_id.value()),
                            std::move(table)).second) {
            return false;
        }
    }

    const auto index_count = data.u32();
    if (index_count > MAX_CATALOG_ITEMS) return false;
    indexes.clear();
    for (std::uint32_t i = 0; i < index_count; ++i) {
        IndexMetadata index;
        const auto index_id = data.u64();
        if (index_id > std::numeric_limits<std::uint32_t>::max()) return false;
        index.index_id = IndexID(index_id);
        index.schema_id = SchemaID(data.u32());
        index.table_id = TableID(data.u64());
        index.name = data.string();
        const auto column_count = data.u32();
        if (column_count > MAX_CATALOG_ITEMS) return false;
        index.indexed_columns.reserve(column_count);
        for (std::uint32_t col = 0; col < column_count; ++col) {
            index.indexed_columns.emplace_back(data.u32());
        }
        index.root_page_id = PageID(data.u64());
        index.tree_height = data.u32();
        const auto unique = data.u8();
        const auto primary = data.u8();
        const auto type = data.u32();
        if (unique > 1 || primary > 1 ||
            type > static_cast<std::uint32_t>(IndexType::BPlusTree)) {
            return false;
        }
        index.is_unique = unique != 0;
        index.is_primary_key = primary != 0;
        index.type = static_cast<IndexType>(type);
        if (!indexes.emplace(static_cast<std::uint32_t>(index.index_id.value()),
                             std::move(index)).second) {
            return false;
        }
    }
    return data.empty();
}

} // namespace

Result<void> CatalogSerializer::save(
    storage::BufferPool* buffer_pool, PageID root_page,
    const std::unordered_map<std::uint32_t, SchemaMetadata>& schemas,
    const std::unordered_map<std::uint32_t, TableMetadata>& tables,
    const std::unordered_map<std::uint32_t, IndexMetadata>& indexes,
    std::uint32_t next_schema, std::uint32_t next_table,
    std::uint32_t next_col, std::uint32_t next_idx, std::uint32_t next_const) {
    if (buffer_pool == nullptr || !root_page.is_valid()) {
        return Result<void>(nemo::Status::InvalidArgument);
    }

    try {
        Writer writer;
        if (!serialize_catalog(schemas, tables, indexes, next_schema, next_table,
                               next_col, next_idx, next_const, writer)) {
            return Result<void>(nemo::Status::InvalidArgument);
        }
        const auto& bytes = writer.data();
        if (bytes.size() > MAX_CATALOG_BYTES) {
            return Result<void>(nemo::Status::InvalidArgument);
        }

        std::vector<PageID> pages;
        if (!read_page_chain(buffer_pool, root_page, pages)) {
            return Result<void>(nemo::Status::Corruption);
        }

        auto root_handle = buffer_pool->fetch_page(root_page);
        if (!root_handle) return Result<void>(root_handle.error());
        const std::size_t payload_size = root_handle.value().get().payload_span().size();
        if (payload_size <= PAGE_LINK_HEADER_SIZE) {
            return Result<void>(nemo::Status::InvalidState);
        }
        const std::size_t chunk_capacity = payload_size - PAGE_LINK_HEADER_SIZE;
        const std::size_t needed_pages = std::max<std::size_t>(
            1, (bytes.size() + chunk_capacity - 1) / chunk_capacity);
        while (pages.size() < needed_pages) {
            auto page = buffer_pool->allocate_page();
            if (!page) return Result<void>(page.error());
            pages.push_back(page.value().get().id());
        }

        std::size_t offset = 0;
        for (std::size_t i = 0; i < pages.size(); ++i) {
            auto handle = buffer_pool->fetch_page(pages[i]);
            if (!handle) return Result<void>(handle.error());
            auto& page = handle.value().get_mut();
            auto header = page.read_header();
            header.page_type = storage::PageType::Metadata;
            header.header_version = 1;

            auto payload = page.payload_span();
            const std::size_t remaining = bytes.size() - offset;
            const std::size_t chunk_size = std::min(chunk_capacity, remaining);
            std::copy(PAGE_MAGIC.begin(), PAGE_MAGIC.end(), payload.begin());
            storage::write_u32_le(payload, 4, static_cast<std::uint32_t>(chunk_size));
            const PageID next = i + 1 < pages.size() ? pages[i + 1] : PageID{};
            storage::write_u64_le(payload, 8, next.value());
            storage::write_u64_le(payload, 16, static_cast<std::uint64_t>(bytes.size()));
            if (chunk_size != 0) {
                std::memcpy(payload.data() + PAGE_LINK_HEADER_SIZE,
                            bytes.data() + offset, chunk_size);
            }
            if (chunk_size < chunk_capacity) {
                std::fill(payload.begin() + static_cast<std::ptrdiff_t>(
                              PAGE_LINK_HEADER_SIZE + chunk_size),
                          payload.end(), std::byte{0});
            }
            offset += chunk_size;
            header.payload_size = static_cast<std::uint32_t>(PAGE_LINK_HEADER_SIZE + chunk_size);
            page.write_header(header);
            handle.value().mark_dirty();
        }
        return {};
    } catch (const std::exception&) {
        return Result<void>(nemo::Status::InvalidArgument);
    }
}

Result<void> CatalogSerializer::load(
    storage::BufferPool* buffer_pool, PageID root_page,
    std::unordered_map<std::uint32_t, SchemaMetadata>& schemas,
    std::unordered_map<std::uint32_t, TableMetadata>& tables,
    std::unordered_map<std::uint32_t, IndexMetadata>& indexes,
    std::uint32_t& next_schema, std::uint32_t& next_table,
    std::uint32_t& next_col, std::uint32_t& next_idx, std::uint32_t& next_const) {
    if (buffer_pool == nullptr || !root_page.is_valid()) {
        return Result<void>(nemo::Status::InvalidArgument);
    }

    try {
        std::vector<PageID> pages;
        if (!read_page_chain(buffer_pool, root_page, pages)) {
            return Result<void>(nemo::Status::Corruption);
        }
        std::vector<std::byte> bytes;
        std::uint64_t expected_size = 0;
        for (const PageID page_id : pages) {
            auto handle = buffer_pool->fetch_page(page_id);
            if (!handle) return Result<void>(handle.error());
            const auto payload = handle.value().get().payload_span();
            if (payload.size() < PAGE_LINK_HEADER_SIZE ||
                !std::equal(PAGE_MAGIC.begin(), PAGE_MAGIC.end(), payload.begin())) {
                return Result<void>(nemo::Status::Corruption);
            }
            const auto chunk_size = storage::read_u32_le(payload, 4);
            const auto page_total = storage::read_u64_le(payload, 16);
            if (chunk_size > payload.size() - PAGE_LINK_HEADER_SIZE ||
                page_total > MAX_CATALOG_BYTES ||
                (expected_size != 0 && page_total != expected_size)) {
                return Result<void>(nemo::Status::Corruption);
            }
            expected_size = page_total;
            if (chunk_size == 0 && bytes.size() < expected_size) {
                return Result<void>(nemo::Status::Corruption);
            }
            if (chunk_size != 0) {
                bytes.insert(bytes.end(),
                    payload.begin() + static_cast<std::ptrdiff_t>(PAGE_LINK_HEADER_SIZE),
                    payload.begin() + static_cast<std::ptrdiff_t>(PAGE_LINK_HEADER_SIZE + chunk_size));
            }
            if (bytes.size() >= expected_size) break;
        }
        if (expected_size == 0 || bytes.size() != expected_size) {
            return Result<void>(nemo::Status::Corruption);
        }
        std::unordered_map<std::uint32_t, SchemaMetadata> loaded_schemas;
        std::unordered_map<std::uint32_t, TableMetadata> loaded_tables;
        std::unordered_map<std::uint32_t, IndexMetadata> loaded_indexes;
        std::uint32_t loaded_next_schema = 0;
        std::uint32_t loaded_next_table = 0;
        std::uint32_t loaded_next_col = 0;
        std::uint32_t loaded_next_idx = 0;
        std::uint32_t loaded_next_const = 0;
        if (!deserialize_catalog(bytes, loaded_schemas, loaded_tables, loaded_indexes,
                                 loaded_next_schema, loaded_next_table, loaded_next_col,
                                 loaded_next_idx, loaded_next_const)) {
            return Result<void>(nemo::Status::Corruption);
        }
        schemas.swap(loaded_schemas);
        tables.swap(loaded_tables);
        indexes.swap(loaded_indexes);
        next_schema = loaded_next_schema;
        next_table = loaded_next_table;
        next_col = loaded_next_col;
        next_idx = loaded_next_idx;
        next_const = loaded_next_const;
        return {};
    } catch (const std::exception&) {
        return Result<void>(nemo::Status::Corruption);
    }
}

} // namespace nemo::catalog
