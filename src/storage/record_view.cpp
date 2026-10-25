#include "nemo/storage/record_view.hpp"
#include "nemo/storage/record_header.hpp"
#include "nemo/types/type_serializer.hpp"
#include <cstring>
#include <bit>

namespace nemo::storage {

namespace {

std::uint32_t read_le32(const std::byte* ptr) {
    std::uint32_t val;
    std::memcpy(&val, ptr, 4);
    if constexpr (std::endian::native == std::endian::big) {
        auto* p = reinterpret_cast<std::uint8_t*>(&val);
        std::swap(p[0], p[3]);
        std::swap(p[1], p[2]);
    }
    return val;
}

std::uint16_t read_le16(const std::byte* ptr) {
    std::uint16_t val;
    std::memcpy(&val, ptr, 2);
    if constexpr (std::endian::native == std::endian::big) {
        auto* p = reinterpret_cast<std::uint8_t*>(&val);
        std::swap(p[0], p[1]);
    }
    return val;
}

} // namespace

RecordView::RecordView(std::span<const std::byte> bytes, const TupleLayout& layout) 
    : m_bytes(bytes), m_layout(layout) 
{
    if (bytes.size() < sizeof(RecordHeader)) {
        throw std::runtime_error("RecordView span too small for header");
    }

    const RecordHeader* header = reinterpret_cast<const RecordHeader*>(bytes.data());
    
    // Convert endianness if necessary for flags
    std::uint16_t flags = read_le16(reinterpret_cast<const std::byte*>(&header->flags));
    
    m_has_nulls = (flags & RecordHeader::FLAG_HAS_NULLS) != 0;
    bool has_varlen = (flags & RecordHeader::FLAG_HAS_VARLEN) != 0;

    std::size_t offset = sizeof(RecordHeader);
    
    // Null bitmap size
    std::size_t null_bitmap_bytes = 0;
    if (m_has_nulls) {
        null_bitmap_bytes = (layout.column_count() + 7) / 8;
    }
    
    m_fixed_data_offset = offset + null_bitmap_bytes;
    m_var_offset_array = m_fixed_data_offset + layout.fixed_part_size();
    
    if (has_varlen) {
        m_var_data_offset = m_var_offset_array + (layout.variable_count() * sizeof(std::uint32_t));
    } else {
        m_var_data_offset = m_var_offset_array;
    }
}

bool RecordView::is_null(std::size_t index) const {
    if (!m_has_nulls) return false;
    
    std::size_t bitmap_offset = sizeof(RecordHeader);
    std::size_t byte_idx = index / 8;
    std::size_t bit_idx = index % 8;
    
    const std::uint8_t* bitmap = reinterpret_cast<const std::uint8_t*>(m_bytes.data() + bitmap_offset);
    return (bitmap[byte_idx] & (1 << bit_idx)) != 0;
}

types::Value RecordView::field(std::size_t index) const {
    if (is_null(index)) {
        return types::Value(m_layout.column(index).type);
    }
    
    const auto& col = m_layout.column(index);
    if (col.fixed_length > 0) {
        // Fixed length
        const std::uint8_t* ptr = reinterpret_cast<const std::uint8_t*>(m_bytes.data() + m_fixed_data_offset + col.offset);
        std::vector<std::uint8_t> buf(ptr, ptr + col.fixed_length);
        return types::TypeSerializer::deserialize(buf, col.type);
    } else {
        // Variable length. Find the index in var array.
        std::uint32_t var_idx = 0;
        for (std::size_t i = 0; i < index; ++i) {
            if (m_layout.column(i).fixed_length == 0 && !is_null(i)) {
                var_idx++;
            }
        }
        
        const std::byte* array_ptr = m_bytes.data() + m_var_offset_array;
        std::uint32_t current_offset = read_le32(array_ptr + var_idx * 4);
        
        // Find next offset to determine length
        std::uint32_t next_offset = static_cast<std::uint32_t>(m_bytes.size() - m_var_data_offset);
        
        std::uint32_t next_var_idx = var_idx + 1;
        
        // Find the next non-null variable field
        for (std::size_t i = index + 1; i < m_layout.column_count(); ++i) {
             if (m_layout.column(i).fixed_length == 0 && !is_null(i)) {
                 next_offset = read_le32(array_ptr + next_var_idx * 4);
                 break;
             }
             if (m_layout.column(i).fixed_length == 0) {
                 next_var_idx++;
             }
        }
        
        std::uint32_t len = next_offset - current_offset;
        const std::uint8_t* ptr = reinterpret_cast<const std::uint8_t*>(m_bytes.data() + m_var_data_offset + current_offset);
        std::vector<std::uint8_t> buf(ptr, ptr + len);
        return types::TypeSerializer::deserialize(buf, col.type);
    }
}

types::ValueView RecordView::field_view(std::size_t index) const {
    if (is_null(index)) {
        return types::ValueView(); // Null view
    }
    
    const auto& col = m_layout.column(index);
    if (col.fixed_length > 0) {
        // Fixed length
        std::span<const std::byte> span(m_bytes.data() + m_fixed_data_offset + col.offset, col.fixed_length);
        return types::ValueView(span, col.type);
    } else {
        // Variable length. Find the index in var array.
        std::uint32_t var_idx = 0;
        for (std::size_t i = 0; i < index; ++i) {
            if (m_layout.column(i).fixed_length == 0 && !is_null(i)) {
                var_idx++;
            }
        }
        
        const std::byte* array_ptr = m_bytes.data() + m_var_offset_array;
        std::uint32_t current_offset = read_le32(array_ptr + var_idx * 4);
        
        std::uint32_t next_offset = static_cast<std::uint32_t>(m_bytes.size() - m_var_data_offset);
        std::uint32_t next_var_idx = var_idx + 1;
        
        for (std::size_t i = index + 1; i < m_layout.column_count(); ++i) {
             if (m_layout.column(i).fixed_length == 0 && !is_null(i)) {
                 next_offset = read_le32(array_ptr + next_var_idx * 4);
                 break;
             }
             if (m_layout.column(i).fixed_length == 0) {
                 next_var_idx++;
             }
        }
        
        std::uint32_t len = next_offset - current_offset;
        
        if (col.type == types::TypeID::Text || col.type == types::TypeID::VarChar || col.type == types::TypeID::Char) {
            std::string_view sv(reinterpret_cast<const char*>(m_bytes.data() + m_var_data_offset + current_offset), len);
            return types::ValueView(sv, col.type);
        } else {
            std::span<const std::byte> span(m_bytes.data() + m_var_data_offset + current_offset, len);
            return types::ValueView(span, col.type);
        }
    }
}

} // namespace nemo::storage
