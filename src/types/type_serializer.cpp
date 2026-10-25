#include "nemo/types/type_serializer.hpp"
#include "nemo/types/array.hpp"
#include "nemo/types/dict.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <bit>

namespace nemo::types {

namespace {

template <typename T>
void write_le(std::vector<std::uint8_t>& buf, T val) {
    // Basic little endian writer
    // C++20 has std::endian
    if constexpr (std::endian::native == std::endian::big) {
        // Swap bytes if native is big endian
        auto* p = reinterpret_cast<std::uint8_t*>(&val);
        for (std::size_t i = 0; i < sizeof(T) / 2; ++i) {
            std::swap(p[i], p[sizeof(T) - 1 - i]);
        }
    }
    
    auto* p = reinterpret_cast<const std::uint8_t*>(&val);
    buf.insert(buf.end(), p, p + sizeof(T));
}

template <typename T>
T read_le(const std::uint8_t* ptr) {
    T val;
    std::memcpy(&val, ptr, sizeof(T));
    if constexpr (std::endian::native == std::endian::big) {
        auto* p = reinterpret_cast<std::uint8_t*>(&val);
        for (std::size_t i = 0; i < sizeof(T) / 2; ++i) {
            std::swap(p[i], p[sizeof(T) - 1 - i]);
        }
    }
    return val;
}

} // namespace

std::vector<std::uint8_t> TypeSerializer::serialize(const Value& value) {
    std::vector<std::uint8_t> res;
    if (value.is_null()) {
        return res; // Empty representation. Caller (record format) manages null bitmap.
    }
    
    switch (value.type()) {
        case TypeID::Boolean:
            res.push_back(value.get<bool>() ? 1 : 0);
            break;
        case TypeID::SmallInt:
            write_le(res, value.get<std::int16_t>());
            break;
        case TypeID::Integer:
            write_le(res, value.get<std::int32_t>());
            break;
        case TypeID::BigInt:
            write_le(res, value.get<std::int64_t>());
            break;
        case TypeID::Real:
            write_le(res, value.get<float>());
            break;
        case TypeID::Double:
            write_le(res, value.get<double>());
            break;
        case TypeID::Numeric: {
            const auto& dec = value.get<Decimal>();
            write_le(res, dec.coefficient());
            res.push_back(dec.scale());
            break;
        }
        case TypeID::Char:
        case TypeID::VarChar:
        case TypeID::Text: {
            const auto& str = value.get<std::string>();
            res.insert(res.end(), str.begin(), str.end());
            break;
        }
        case TypeID::Bytea: {
            const auto& b = value.get<std::vector<std::uint8_t>>();
            res.insert(res.end(), b.begin(), b.end());
            break;
        }
        case TypeID::Date:
            write_le(res, value.get<Date>().days());
            break;
        case TypeID::Time:
            write_le(res, value.get<Time>().microseconds());
            break;
        case TypeID::Timestamp:
        case TypeID::TimestampTZ:
            write_le(res, value.get<Timestamp>().microseconds());
            break;
        case TypeID::Interval: {
            const auto& iv = value.get<Interval>();
            write_le(res, iv.months());
            write_le(res, iv.days());
            write_le(res, iv.microseconds());
            break;
        }
        case TypeID::UUID: {
            const auto& u = value.get<UUID>();
            res.insert(res.end(), u.data().begin(), u.data().end());
            break;
        }
        case TypeID::JSON: {
            const auto& j = value.get<Json>();
            // V1: serialize as string for simplicity.
            std::string s = j.to_string();
            res.insert(res.end(), s.begin(), s.end());
            break;
        }
        case TypeID::Array: {
            const auto& arr = value.get<Array>();
            write_le<std::uint32_t>(res, static_cast<std::uint32_t>(arr.size()));
            for (std::size_t i = 0; i < arr.size(); ++i) {
                const auto& elem = arr.at(i);
                std::uint16_t tid = static_cast<std::uint16_t>(elem.type());
                write_le<std::uint16_t>(res, tid);
                auto elem_bytes = TypeSerializer::serialize(elem);
                write_le<std::uint32_t>(res, static_cast<std::uint32_t>(elem_bytes.size()));
                res.insert(res.end(), elem_bytes.begin(), elem_bytes.end());
            }
            break;
        }
        case TypeID::DICT: {
            const auto& dict = value.get<Dict>();
            std::vector<std::string> keys = dict.keys();
            std::sort(keys.begin(), keys.end()); // Canonical order
            
            write_le<std::uint32_t>(res, static_cast<std::uint32_t>(keys.size()));
            for (const auto& k : keys) {
                write_le<std::uint32_t>(res, static_cast<std::uint32_t>(k.size()));
                res.insert(res.end(), k.begin(), k.end());
                
                const auto& elem = *dict.get(k);
                std::uint16_t tid = static_cast<std::uint16_t>(elem.type());
                write_le<std::uint16_t>(res, tid);
                auto elem_bytes = TypeSerializer::serialize(elem);
                write_le<std::uint32_t>(res, static_cast<std::uint32_t>(elem_bytes.size()));
                res.insert(res.end(), elem_bytes.begin(), elem_bytes.end());
            }
            break;
        }
        case TypeID::JSONB: {
            std::string s = value.to_string();
            res.insert(res.end(), s.begin(), s.end());
            break;
        }
        case TypeID::Enum:
        case TypeID::Bit:
        case TypeID::VarBit:
        case TypeID::Inet:
        case TypeID::Cidr:
        case TypeID::MacAddr:
        case TypeID::Range: {
            std::string s = value.to_string();
            res.insert(res.end(), s.begin(), s.end());
            break;
        }
        case TypeID::Null:
        case TypeID::Invalid:
            throw std::runtime_error("Serialization not fully implemented for this type in V1");
    }
    
    return res;
}

Value TypeSerializer::deserialize(const std::vector<std::uint8_t>& bytes, TypeID type) {
    // If bytes is empty, we must rely on the caller/record format. 
    // Here we assume if bytes are passed, it's not null.
    
    const std::uint8_t* ptr = bytes.data();
    std::size_t size = bytes.size();
    
    switch (type) {
        case TypeID::Boolean:
            if (size < 1) throw std::runtime_error("Buffer too small");
            return Value(ptr[0] != 0);
        case TypeID::SmallInt:
            if (size < 2) throw std::runtime_error("Buffer too small");
            return Value(read_le<std::int16_t>(ptr));
        case TypeID::Integer:
            if (size < 4) throw std::runtime_error("Buffer too small");
            return Value(read_le<std::int32_t>(ptr));
        case TypeID::BigInt:
            if (size < 8) throw std::runtime_error("Buffer too small");
            return Value(read_le<std::int64_t>(ptr));
        case TypeID::Real:
            if (size < 4) throw std::runtime_error("Buffer too small");
            return Value(read_le<float>(ptr));
        case TypeID::Double:
            if (size < 8) throw std::runtime_error("Buffer too small");
            return Value(read_le<double>(ptr));
        case TypeID::Numeric:
            if (size < 9) throw std::runtime_error("Buffer too small");
            return Value(Decimal(read_le<std::int64_t>(ptr), ptr[8]));
        case TypeID::Char:
        case TypeID::VarChar:
        case TypeID::Text:
            return Value(std::string(reinterpret_cast<const char*>(ptr), size), type);
        case TypeID::Bytea:
            return Value(std::vector<std::uint8_t>(ptr, ptr + size));
        case TypeID::Date:
            if (size < 4) throw std::runtime_error("Buffer too small");
            return Value(Date(read_le<std::int32_t>(ptr)));
        case TypeID::Time:
            if (size < 8) throw std::runtime_error("Buffer too small");
            return Value(Time(read_le<std::int64_t>(ptr)));
        case TypeID::Timestamp:
        case TypeID::TimestampTZ:
            if (size < 8) throw std::runtime_error("Buffer too small");
            return Value(Timestamp(read_le<std::int64_t>(ptr)));
        case TypeID::Interval:
            if (size < 16) throw std::runtime_error("Buffer too small");
            return Value(Interval(read_le<std::int32_t>(ptr), read_le<std::int32_t>(ptr+4), read_le<std::int64_t>(ptr+8)));
        case TypeID::UUID: {
            if (size < 16) throw std::runtime_error("Buffer too small");
            std::array<std::uint8_t, 16> arr;
            std::memcpy(arr.data(), ptr, 16);
            return Value(UUID(arr));
        }
        case TypeID::JSON:
            return Value(Json::parse(std::string(reinterpret_cast<const char*>(ptr), size)));
        case TypeID::Array: {
            if (size < 4) throw std::runtime_error("Buffer too small for Array");
            std::uint32_t count = read_le<std::uint32_t>(ptr);
            std::size_t offset = 4;
            Array arr(TypeID::Invalid);
            for (std::uint32_t i = 0; i < count; ++i) {
                if (offset + 2 > size) throw std::runtime_error("Buffer too small for Array elem type");
                std::uint16_t tid = read_le<std::uint16_t>(ptr + offset);
                offset += 2;
                if (offset + 4 > size) throw std::runtime_error("Buffer too small for Array element size");
                std::uint32_t elem_size = read_le<std::uint32_t>(ptr + offset);
                offset += 4;
                if (offset + elem_size > size) throw std::runtime_error("Buffer too small for Array element");
                
                std::vector<std::uint8_t> elem_bytes(ptr + offset, ptr + offset + elem_size);
                arr.push_back(TypeSerializer::deserialize(elem_bytes, static_cast<TypeID>(tid)));
                offset += elem_size;
            }
            return Value(std::move(arr));
        }
        case TypeID::DICT: {
            if (size < 4) throw std::runtime_error("Buffer too small for Dict");
            std::uint32_t count = read_le<std::uint32_t>(ptr);
            std::size_t offset = 4;
            Dict dict;
            for (std::uint32_t i = 0; i < count; ++i) {
                if (offset + 4 > size) throw std::runtime_error("Buffer too small for Dict key size");
                std::uint32_t k_size = read_le<std::uint32_t>(ptr + offset);
                offset += 4;
                if (offset + k_size > size) throw std::runtime_error("Buffer too small for Dict key");
                std::string k(reinterpret_cast<const char*>(ptr + offset), k_size);
                offset += k_size;
                
                if (offset + 2 > size) throw std::runtime_error("Buffer too small for Dict elem type");
                std::uint16_t tid = read_le<std::uint16_t>(ptr + offset);
                offset += 2;
                if (offset + 4 > size) throw std::runtime_error("Buffer too small for Dict element size");
                std::uint32_t elem_size = read_le<std::uint32_t>(ptr + offset);
                offset += 4;
                if (offset + elem_size > size) throw std::runtime_error("Buffer too small for Dict element");
                
                std::vector<std::uint8_t> elem_bytes(ptr + offset, ptr + offset + elem_size);
                dict.set(k, TypeSerializer::deserialize(elem_bytes, static_cast<TypeID>(tid)));
                offset += elem_size;
            }
            return Value(std::move(dict));
        }
        case TypeID::JSONB:
            return Value(std::string(reinterpret_cast<const char*>(ptr), size), TypeID::JSONB);
        case TypeID::Enum:
            return Value(std::string(reinterpret_cast<const char*>(ptr), size), TypeID::Enum);
        case TypeID::Bit:
            return Value(std::string(reinterpret_cast<const char*>(ptr), size), TypeID::Bit);
        case TypeID::VarBit:
            return Value(std::string(reinterpret_cast<const char*>(ptr), size), TypeID::VarBit);
        case TypeID::Inet:
            return Value(std::string(reinterpret_cast<const char*>(ptr), size), TypeID::Inet);
        case TypeID::Cidr:
            return Value(std::string(reinterpret_cast<const char*>(ptr), size), TypeID::Cidr);
        case TypeID::MacAddr:
            return Value(std::string(reinterpret_cast<const char*>(ptr), size), TypeID::MacAddr);
        case TypeID::Range:
            return Value(std::string(reinterpret_cast<const char*>(ptr), size), TypeID::Range);
        default:
            throw std::runtime_error("Deserialization not fully implemented for this type in V1");
    }
}

} // namespace nemo::types
