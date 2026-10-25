#ifndef NEMO_CONCURRENCY_LOCK_RESOURCE_HPP
#define NEMO_CONCURRENCY_LOCK_RESOURCE_HPP

#include "nemo/core/ids.hpp"
#include "nemo/storage/record_id.hpp"
#include <cstdint>
#include <functional>

namespace nemo::concurrency {

enum class LockResourceType {
    Database,
    Schema,
    Table,
    Page,
    Row,
    Cell
};

struct LockResource {
    LockResourceType type;
    std::uint32_t database_id{0};
    std::uint32_t schema_id{0};
    std::uint32_t table_id{0};
    std::uint32_t page_id{static_cast<std::uint32_t>(nemo::PageID::invalid_value())};
    std::uint32_t row_id{nemo::storage::RecordID::INVALID_SLOT};
    std::uint32_t column_id{0};

    bool operator==(const LockResource& other) const {
        if (type != other.type) return false;
        if (database_id != other.database_id) return false;
        
        switch (type) {
            case LockResourceType::Database: return true;
            case LockResourceType::Schema: return schema_id == other.schema_id;
            case LockResourceType::Table: return schema_id == other.schema_id && table_id == other.table_id;
            case LockResourceType::Page: return schema_id == other.schema_id && table_id == other.table_id && page_id == other.page_id;
            case LockResourceType::Row: return schema_id == other.schema_id && table_id == other.table_id && page_id == other.page_id && row_id == other.row_id;
            case LockResourceType::Cell: return schema_id == other.schema_id && table_id == other.table_id && page_id == other.page_id && row_id == other.row_id && column_id == other.column_id;
        }
        return false;
    }
};

} // namespace nemo::concurrency

namespace std {
    template <>
    struct hash<nemo::concurrency::LockResource> {
        std::size_t operator()(const nemo::concurrency::LockResource& resource) const {
            std::size_t h = std::hash<int>{}(static_cast<int>(resource.type));
            h ^= std::hash<std::uint32_t>{}(resource.database_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
            
            if (resource.type >= nemo::concurrency::LockResourceType::Schema)
                h ^= std::hash<std::uint32_t>{}(resource.schema_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
            if (resource.type >= nemo::concurrency::LockResourceType::Table)
                h ^= std::hash<std::uint32_t>{}(resource.table_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
            if (resource.type >= nemo::concurrency::LockResourceType::Page)
                h ^= std::hash<std::uint32_t>{}(resource.page_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
            if (resource.type >= nemo::concurrency::LockResourceType::Row)
                h ^= std::hash<std::uint32_t>{}(resource.row_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
            if (resource.type >= nemo::concurrency::LockResourceType::Cell)
                h ^= std::hash<std::uint32_t>{}(resource.column_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
                
            return h;
        }
    };
} // namespace std

#endif // NEMO_CONCURRENCY_LOCK_RESOURCE_HPP
