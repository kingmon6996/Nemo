#include "nemo/storage/record_serializer.hpp"
#include "nemo/storage/record_header.hpp"
#include "nemo/types/type_serializer.hpp"
#include <cstring>
#include <bit>
#include <stdexcept>

namespace nemo::storage {


std::vector<std::byte> RecordSerializer::serialize(const Record& record, const TupleLayout& layout) {
    if (record.field_count() != layout.column_count()) {
        throw std::invalid_argument("Record field count does not match layout");
    }

    std::vector<std::byte> buffer;
    
    bool has_nulls = false;
    for (std::size_t i = 0; i < record.field_count(); ++i) {
        if (record.field(i).is_null()) {
            has_nulls = true;
            break;
        }
    }
    
    bool has_varlen = layout.variable_count() > 0;

    // 1. Write Header placeholder
    std::size_t header_offset = 0;
    buffer.resize(sizeof(RecordHeader), std::byte{0});
    
    // 2. Write NULL Bitmap if needed
    if (has_nulls) {
        std::size_t null_bitmap_bytes = (layout.column_count() + 7) / 8;
        std::vector<std::byte> bitmap(null_bitmap_bytes, std::byte{0});
        
        for (std::size_t i = 0; i < layout.column_count(); ++i) {
            if (record.field(i).is_null()) {
                std::size_t byte_idx = i / 8;
                std::size_t bit_idx = i % 8;
                bitmap[byte_idx] |= static_cast<std::byte>(1 << bit_idx);
            }
        }
        buffer.insert(buffer.end(), bitmap.begin(), bitmap.end());
    }

    // 3. Write fixed-size fields
    // Pre-allocate fixed area
    std::size_t fixed_area_start = buffer.size();
    buffer.insert(buffer.end(), layout.fixed_part_size(), std::byte{0});
    
    for (std::size_t i = 0; i < layout.column_count(); ++i) {
        const auto& col = layout.column(i);
        if (col.fixed_length > 0 && !record.field(i).is_null()) {
            auto bytes = types::TypeSerializer::serialize(record.field(i));
            if (bytes.size() != col.fixed_length) {
                throw std::runtime_error("Fixed length serialization mismatch");
            }
            std::memcpy(buffer.data() + fixed_area_start + col.offset, bytes.data(), bytes.size());
        }
    }
    
    // 4. Write variable-size fields
    if (has_varlen) {
        std::size_t var_offset_array_start = buffer.size();
        
        // We will only write offsets for non-null variable fields.
        std::uint32_t active_var_fields = 0;
        for (std::size_t i = 0; i < layout.column_count(); ++i) {
            if (layout.column(i).fixed_length == 0 && !record.field(i).is_null()) {
                active_var_fields++;
            }
        }
        
        // Pre-allocate offsets array
        std::vector<std::byte> var_offsets(active_var_fields * 4, std::byte{0});
        buffer.insert(buffer.end(), var_offsets.begin(), var_offsets.end());
        
        std::uint32_t var_idx = 0;
        std::uint32_t current_var_data_offset = 0;
        
        for (std::size_t i = 0; i < layout.column_count(); ++i) {
            const auto& col = layout.column(i);
            if (col.fixed_length == 0 && !record.field(i).is_null()) {
                // Write offset to the offset array
                std::uint32_t offset_val = current_var_data_offset;
                
                std::byte* ptr = buffer.data() + var_offset_array_start + (var_idx * 4);
                if constexpr (std::endian::native == std::endian::big) {
                    std::uint32_t swapped = offset_val;
                    auto* p = reinterpret_cast<std::uint8_t*>(&swapped);
                    std::swap(p[0], p[3]);
                    std::swap(p[1], p[2]);
                    std::memcpy(ptr, &swapped, 4);
                } else {
                    std::memcpy(ptr, &offset_val, 4);
                }
                
                auto bytes = types::TypeSerializer::serialize(record.field(i));
                const std::byte* bptr = reinterpret_cast<const std::byte*>(bytes.data());
                buffer.insert(buffer.end(), bptr, bptr + bytes.size());
                
                current_var_data_offset += static_cast<std::uint32_t>(bytes.size());
                var_idx++;
            }
        }
    }

    // Backfill header
    RecordHeader header;
    header.size = static_cast<std::uint32_t>(buffer.size());
    header.set_has_nulls(has_nulls);
    header.set_has_varlen(has_varlen);
    
    // Convert endianness if necessary
    if constexpr (std::endian::native == std::endian::big) {
        auto* p32 = reinterpret_cast<std::uint8_t*>(&header.size);
        std::swap(p32[0], p32[3]);
        std::swap(p32[1], p32[2]);
        
        auto* p16 = reinterpret_cast<std::uint8_t*>(&header.flags);
        std::swap(p16[0], p16[1]);
    }
    
    std::memcpy(buffer.data() + header_offset, &header, sizeof(RecordHeader));
    
    return buffer;
}

} // namespace nemo::storage
