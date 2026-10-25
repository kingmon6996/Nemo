#include "nemo/index/index_key.hpp"
#include "nemo/types/type_serializer.hpp"
#include <cstring>
#include <stdexcept>
#include <bit>

namespace nemo::index {

namespace {

void write_le32(std::vector<std::byte>& buf, std::uint32_t val) {
    if constexpr (std::endian::native == std::endian::big) {
        auto* p = reinterpret_cast<std::uint8_t*>(&val);
        std::swap(p[0], p[3]);
        std::swap(p[1], p[2]);
    }
    const std::byte* p = reinterpret_cast<const std::byte*>(&val);
    buf.insert(buf.end(), p, p + 4);
}

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

} // namespace

IndexKey::IndexKey(types::Value val) {
    m_values.push_back(std::move(val));
}

IndexKey::IndexKey(std::vector<types::Value> values) : m_values(std::move(values)) {}

std::vector<std::byte> IndexKey::serialize() const {
    std::vector<std::byte> buffer;
    
    // We encode the number of values (composite key support)
    // Though usually the index schema defines this, storing it allows variable-length prefix searches later.
    std::uint32_t count = static_cast<std::uint32_t>(m_values.size());
    write_le32(buffer, count);
    
    // For each value, we need to know its length because IndexKey strings can be arbitrary.
    for (const auto& val : m_values) {
        // NULL representation: length = max
        if (val.is_null()) {
            write_le32(buffer, 0xFFFFFFFF);
            continue;
        }
        
        auto bytes = types::TypeSerializer::serialize(val);
        write_le32(buffer, static_cast<std::uint32_t>(bytes.size()));
        const auto* bytes_ptr = reinterpret_cast<const std::byte*>(bytes.data());
        buffer.insert(buffer.end(), bytes_ptr, bytes_ptr + bytes.size());
    }
    
    return buffer;
}

IndexKey IndexKey::deserialize(std::span<const std::byte> bytes, const std::vector<types::TypeID>& types) {
    if (bytes.size() < 4) {
        throw std::runtime_error("IndexKey span too small");
    }
    
    std::uint32_t count = read_le32(bytes.data());
    if (count > types.size()) {
        throw std::runtime_error("IndexKey deserialization mismatch with schema type count");
    }
    
    std::vector<types::Value> values;
    values.reserve(count);
    
    std::size_t offset = 4;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (offset + 4 > bytes.size()) throw std::runtime_error("IndexKey bounds error");
        
        std::uint32_t len = read_le32(bytes.data() + offset);
        offset += 4;
        
        if (len == 0xFFFFFFFF) {
            // It's a NULL
            values.push_back(types::Value(types[i]));
        } else {
            if (offset + len > bytes.size()) throw std::runtime_error("IndexKey bounds error on payload");
            
            std::span<const std::byte> val_span(bytes.data() + offset, len);
            
            // In a real system we might avoid allocation, but IndexKey is usually fully materialized for search
            std::vector<std::uint8_t> val_buf(
                reinterpret_cast<const std::uint8_t*>(val_span.data()),
                reinterpret_cast<const std::uint8_t*>(val_span.data()) + len
            );
            values.push_back(types::TypeSerializer::deserialize(val_buf, types[i]));
            
            offset += len;
        }
    }
    
    return IndexKey(std::move(values));
}

} // namespace nemo::index
