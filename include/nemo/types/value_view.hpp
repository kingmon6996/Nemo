#ifndef NEMO_TYPES_VALUE_VIEW_HPP
#define NEMO_TYPES_VALUE_VIEW_HPP

#include "nemo/types/type_id.hpp"
#include <string_view>
#include <span>

namespace nemo::types {

// A non-owning view over a serialized value (or a temporary reference).
// Extremely important for high-performance reading directly from BufferPool pages
// without making std::string copies.
class ValueView {
public:
    ValueView() noexcept : m_type(TypeID::Null), m_is_null(true) {}
    
    // View over a string/text
    ValueView(std::string_view sv, TypeID type = TypeID::Text) noexcept 
        : m_type(type), m_is_null(false), m_str_view(sv) {}

    // View over a binary blob
    ValueView(std::span<const std::byte> bytes, TypeID type = TypeID::Bytea) noexcept
        : m_type(type), m_is_null(false), m_bytes_view(bytes) {}

    TypeID type() const noexcept { return m_type; }
    bool is_null() const noexcept { return m_is_null; }
    
    std::string_view as_string_view() const noexcept { return m_str_view; }
    std::span<const std::byte> as_bytes() const noexcept { return m_bytes_view; }

private:
    TypeID m_type;
    bool m_is_null;
    std::string_view m_str_view;
    std::span<const std::byte> m_bytes_view;
};

} // namespace nemo::types

#endif // NEMO_TYPES_VALUE_VIEW_HPP
