#include "nemo/types/type_cast.hpp"
#include <string>
#include <stdexcept>
#include <limits>
#include <cmath>

namespace nemo::types {

std::optional<Value> TypeCast::cast(const Value& value, TypeID target_type) {
    if (value.is_null()) {
        return Value(target_type);
    }
    
    if (value.type() == target_type) {
        return value;
    }
    
    // Simplistic casting logic for V1.
    // Real engines have large conversion matrices.
    
    // Numeric -> Numeric
    if (target_type == TypeID::BigInt) {
        if (value.type() == TypeID::Integer) return Value(static_cast<std::int64_t>(value.get<std::int32_t>()));
        if (value.type() == TypeID::SmallInt) return Value(static_cast<std::int64_t>(value.get<std::int16_t>()));
        if (value.type() == TypeID::Double) return Value(static_cast<std::int64_t>(value.get<double>()));
    }
    
    if (target_type == TypeID::Integer) {
        if (value.type() == TypeID::BigInt) {
            auto v = value.get<std::int64_t>();
            if (v > std::numeric_limits<std::int32_t>::max() || v < std::numeric_limits<std::int32_t>::min()) return std::nullopt; // Overflow
            return Value(static_cast<std::int32_t>(v));
        }
    }
    
    if (target_type == TypeID::Double) {
        if (value.type() == TypeID::Integer) return Value(static_cast<double>(value.get<std::int32_t>()));
        if (value.type() == TypeID::BigInt) return Value(static_cast<double>(value.get<std::int64_t>()));
    }
    
    // String parsing
    if (value.type() == TypeID::Text || value.type() == TypeID::VarChar || value.type() == TypeID::Char) {
        const auto& str = value.get<std::string>();
        try {
            if (target_type == TypeID::Integer) return Value(static_cast<std::int32_t>(std::stoi(str)));
            if (target_type == TypeID::BigInt) return Value(static_cast<std::int64_t>(std::stoll(str)));
            if (target_type == TypeID::Double) return Value(std::stod(str));
            if (target_type == TypeID::UUID) {
                auto u = UUID::from_string(str);
                if (u) return Value(*u);
                return std::nullopt;
            }
        } catch (...) {
            return std::nullopt; // Parse failure
        }
    }
    
    // Boolean conversions
    if (target_type == TypeID::Boolean) {
        if (value.type() == TypeID::Integer) return Value(value.get<std::int32_t>() != 0);
        if (value.type() == TypeID::BigInt) return Value(value.get<std::int64_t>() != 0);
        if (value.type() == TypeID::SmallInt) return Value(value.get<std::int16_t>() != 0);
        if (value.type() == TypeID::Text || value.type() == TypeID::VarChar || value.type() == TypeID::Char) {
            std::string s = value.get<std::string>();
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (s == "true" || s == "t" || s == "1" || s == "yes") return Value(true);
            if (s == "false" || s == "f" || s == "0" || s == "no") return Value(false);
            return std::nullopt;
        }
    }
    if (value.type() == TypeID::Boolean) {
        bool b = value.get<bool>();
        if (target_type == TypeID::Integer) return Value(static_cast<std::int32_t>(b ? 1 : 0));
        if (target_type == TypeID::BigInt) return Value(static_cast<std::int64_t>(b ? 1 : 0));
        if (target_type == TypeID::SmallInt) return Value(static_cast<std::int16_t>(b ? 1 : 0));
        if (target_type == TypeID::Text || target_type == TypeID::VarChar || target_type == TypeID::Char) {
            return Value(b ? "true" : "false", target_type);
        }
    }

    // JSON <-> JSONB
    if ((target_type == TypeID::JSON || target_type == TypeID::JSONB) &&
        (value.type() == TypeID::JSON || value.type() == TypeID::JSONB ||
         value.type() == TypeID::Text || value.type() == TypeID::VarChar)) {
        return Value(value.to_string(), target_type);
    }

    // Cast to String
    if (target_type == TypeID::Text || target_type == TypeID::VarChar || target_type == TypeID::Char) {
        return Value(value.to_string(), target_type);
    }

    // String parsing to domain types
    if (value.type() == TypeID::Text || value.type() == TypeID::VarChar || value.type() == TypeID::Char) {
        const auto& str = value.get<std::string>();
        try {
            if (target_type == TypeID::Integer) return Value(static_cast<std::int32_t>(std::stoi(str)));
            if (target_type == TypeID::BigInt) return Value(static_cast<std::int64_t>(std::stoll(str)));
            if (target_type == TypeID::SmallInt) return Value(static_cast<std::int16_t>(std::stoi(str)));
            if (target_type == TypeID::Double) return Value(std::stod(str));
            if (target_type == TypeID::Real) return Value(static_cast<float>(std::stof(str)));
            if (target_type == TypeID::UUID) {
                auto u = UUID::from_string(str);
                if (u) return Value(*u);
                return std::nullopt;
            }
            if (target_type == TypeID::Inet || target_type == TypeID::Cidr || target_type == TypeID::MacAddr ||
                target_type == TypeID::Bit || target_type == TypeID::VarBit || target_type == TypeID::Range ||
                target_type == TypeID::Enum) {
                return Value(str, target_type);
            }
        } catch (...) {
            return std::nullopt; // Parse failure
        }
    }
    
    return std::nullopt;
}

} // namespace nemo::types
