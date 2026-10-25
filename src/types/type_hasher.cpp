#include "nemo/types/type_hasher.hpp"
#include "nemo/types/array.hpp"
#include "nemo/types/dict.hpp"
#include <functional>

namespace nemo::types {

std::size_t TypeHasher::hash(const Value& value) noexcept {
    if (value.is_null()) {
        return 0; // Deterministic null hash
    }
    
    // For V1 we use std::hash on the underlying value types.
    // In production, we'd use MurmurHash3 on the serialized bytes to ensure
    // memory-layout independent stable hashing across processes.
    
    switch (value.type()) {
        case TypeID::Boolean: return std::hash<bool>()(value.get<bool>());
        case TypeID::SmallInt: return std::hash<std::int16_t>()(value.get<std::int16_t>());
        case TypeID::Integer: return std::hash<std::int32_t>()(value.get<std::int32_t>());
        case TypeID::BigInt: return std::hash<std::int64_t>()(value.get<std::int64_t>());
        case TypeID::Real: return std::hash<float>()(value.get<float>());
        case TypeID::Double: return std::hash<double>()(value.get<double>());
        case TypeID::Numeric: {
            const auto& dec = value.get<Decimal>();
            return std::hash<std::int64_t>()(dec.coefficient()) ^ (std::hash<std::uint8_t>()(dec.scale()) << 1);
        }
        case TypeID::Char:
        case TypeID::VarChar:
        case TypeID::Text:
            return std::hash<std::string>()(value.get<std::string>());
        case TypeID::Bytea: {
            const auto& b = value.get<std::vector<std::uint8_t>>();
            std::size_t h = 0;
            for (auto byte : b) {
                h ^= std::hash<std::uint8_t>()(byte) + 0x9e3779b9 + (h << 6) + (h >> 2);
            }
            return h;
        }
        case TypeID::Date: return std::hash<std::int32_t>()(value.get<Date>().days());
        case TypeID::Time: return std::hash<std::int64_t>()(value.get<Time>().microseconds());
        case TypeID::Timestamp:
        case TypeID::TimestampTZ:
            return std::hash<std::int64_t>()(value.get<Timestamp>().microseconds());
        case TypeID::Interval: {
            const auto& iv = value.get<Interval>();
            return std::hash<std::int32_t>()(iv.months()) ^ 
                   (std::hash<std::int32_t>()(iv.days()) << 1) ^ 
                   (std::hash<std::int64_t>()(iv.microseconds()) << 2);
        }
        case TypeID::UUID: return std::hash<UUID>()(value.get<UUID>());
        case TypeID::JSON:
            return std::hash<std::string>()(value.get<Json>().to_string());
        case TypeID::Array:
            return std::hash<std::string>()(value.get<Array>().to_string());
        case TypeID::DICT:
            return std::hash<std::string>()(value.get<Dict>().to_string());
        default:
            return 0; // Not hashable
    }
}

} // namespace nemo::types
