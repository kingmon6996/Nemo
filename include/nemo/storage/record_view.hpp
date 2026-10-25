#ifndef NEMO_STORAGE_RECORD_VIEW_HPP
#define NEMO_STORAGE_RECORD_VIEW_HPP

#include "nemo/types/value_view.hpp"
#include "nemo/types/value.hpp"
#include "nemo/storage/tuple_layout.hpp"
#include <cstdint>
#include <span>
#include <stdexcept>

namespace nemo::storage {

// A lightweight, non-owning view over a serialized record.
// Resolves fields lazily directly from the page memory to avoid full materialization.
class RecordView {
public:
    // bytes should span the entire physical record including the header
    RecordView(std::span<const std::byte> bytes, const TupleLayout& layout);

    std::size_t field_count() const noexcept { return m_layout.column_count(); }

    bool is_null(std::size_t index) const;

    // Fast-path for retrieving variable length views or simple primitives.
    // Note: Primitive value extraction requires type instantiation, so returning Value
    // is safer and often zero-allocation, but for strings/bytea it returns a ValueView wrapper 
    // to avoid copying strings. We return `Value` for primitives and `ValueView` for var-length.
    // To unify the API easily for now, we'll return a variant or we can just materialize a Value
    // because primitive Values don't allocate on heap. We will materialize Value.
    
    types::Value field(std::size_t index) const;
    
    // Returns a ValueView. If it's a fixed length, the view points into the buffer.
    types::ValueView field_view(std::size_t index) const;

private:
    std::span<const std::byte> m_bytes;
    const TupleLayout& m_layout;
    std::size_t m_fixed_data_offset;
    std::size_t m_var_offset_array;
    std::size_t m_var_data_offset;
    bool m_has_nulls;
};

} // namespace nemo::storage

#endif // NEMO_STORAGE_RECORD_VIEW_HPP
