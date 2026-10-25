#ifndef NEMO_STORAGE_RECORD_HEADER_HPP
#define NEMO_STORAGE_RECORD_HEADER_HPP

#include <cstdint>

#include "nemo/transaction/mvcc_header.hpp"

namespace nemo::storage {

// The header at the beginning of each physical record on a page.
struct RecordHeader {
    std::uint32_t size{0}; // Size of the record data (including header)
    
    // Bitmask for flags (e.g. is_deleted, has_nulls, has_variable_data)
    std::uint16_t flags{0};
    
    // MVCC specific metadata
    transaction::MVCCHeader mvcc;

    // Flag constants
    static constexpr std::uint16_t FLAG_DELETED = 1 << 0;
    static constexpr std::uint16_t FLAG_HAS_NULLS = 1 << 1;
    static constexpr std::uint16_t FLAG_HAS_VARLEN = 1 << 2;

    bool is_deleted() const noexcept { return (flags & FLAG_DELETED) != 0; }
    void set_deleted(bool v) noexcept { 
        if (v) flags = static_cast<std::uint16_t>(flags | FLAG_DELETED); 
        else flags = static_cast<std::uint16_t>(flags & ~FLAG_DELETED); 
    }

    bool has_nulls() const noexcept { return (flags & FLAG_HAS_NULLS) != 0; }
    void set_has_nulls(bool v) noexcept { 
        if (v) flags = static_cast<std::uint16_t>(flags | FLAG_HAS_NULLS); 
        else flags = static_cast<std::uint16_t>(flags & ~FLAG_HAS_NULLS); 
    }

    bool has_varlen() const noexcept { return (flags & FLAG_HAS_VARLEN) != 0; }
    void set_has_varlen(bool v) noexcept { 
        if (v) flags = static_cast<std::uint16_t>(flags | FLAG_HAS_VARLEN); 
        else flags = static_cast<std::uint16_t>(flags & ~FLAG_HAS_VARLEN); 
    }
};

} // namespace nemo::storage

#endif // NEMO_STORAGE_RECORD_HEADER_HPP
