#include "nemo/types/value.hpp"
#include "nemo/types/array.hpp"
#include "nemo/types/dict.hpp"
#include <stdexcept>
#include <iomanip>
#include <sstream>

namespace nemo::types {

Value::Value() noexcept : m_type(TypeID::Null), m_is_null(true), m_data(std::monostate{}) {}

Value::Value(TypeID type) noexcept : m_type(type), m_is_null(true), m_data(std::monostate{}) {}

Value::Value(bool val) noexcept : m_type(TypeID::Boolean), m_is_null(false), m_data(val) {}
Value::Value(std::int16_t val) noexcept : m_type(TypeID::SmallInt), m_is_null(false), m_data(val) {}
Value::Value(std::int32_t val) noexcept : m_type(TypeID::Integer), m_is_null(false), m_data(val) {}
Value::Value(std::int64_t val) noexcept : m_type(TypeID::BigInt), m_is_null(false), m_data(val) {}
Value::Value(float val) noexcept : m_type(TypeID::Real), m_is_null(false), m_data(val) {}
Value::Value(double val) noexcept : m_type(TypeID::Double), m_is_null(false), m_data(val) {}

Value::Value(Decimal val) noexcept : m_type(TypeID::Numeric), m_is_null(false), m_data(val) {}
Value::Value(std::string val, TypeID type) : m_type(type), m_is_null(false), m_data(std::move(val)) {}
Value::Value(std::vector<std::uint8_t> val) : m_type(TypeID::Bytea), m_is_null(false), m_data(std::move(val)) {}

Value::Value(Date val) noexcept : m_type(TypeID::Date), m_is_null(false), m_data(val) {}
Value::Value(Time val) noexcept : m_type(TypeID::Time), m_is_null(false), m_data(val) {}
Value::Value(Timestamp val, bool with_tz) noexcept 
    : m_type(with_tz ? TypeID::TimestampTZ : TypeID::Timestamp), m_is_null(false), m_data(val) {}
Value::Value(Interval val) noexcept : m_type(TypeID::Interval), m_is_null(false), m_data(val) {}
Value::Value(UUID val) noexcept : m_type(TypeID::UUID), m_is_null(false), m_data(val) {}
Value::Value(Json val) : m_type(TypeID::JSON), m_is_null(false), m_data(std::move(val)) {}

Value::Value(Array val) : m_type(TypeID::Array), m_is_null(false), m_data(std::make_shared<Array>(std::move(val))) {}
Value::Value(Dict val) : m_type(TypeID::DICT), m_is_null(false), m_data(std::make_shared<Dict>(std::move(val))) {}

Value::Value(std::uint32_t enum_id, std::string label) : m_type(TypeID::Enum), m_is_null(false), m_data(std::make_pair(enum_id, std::move(label))) {}

Value::Value(const Value& other) = default;
Value& Value::operator=(const Value& other) = default;
Value::Value(Value&& other) noexcept = default;
Value& Value::operator=(Value&& other) noexcept = default;
Value::~Value() = default;


template <> const bool& Value::get<bool>() const { return std::get<bool>(m_data); }
template <> const std::int16_t& Value::get<std::int16_t>() const { return std::get<std::int16_t>(m_data); }
template <> const std::int32_t& Value::get<std::int32_t>() const { return std::get<std::int32_t>(m_data); }
template <> const std::int64_t& Value::get<std::int64_t>() const { return std::get<std::int64_t>(m_data); }
template <> const float& Value::get<float>() const { return std::get<float>(m_data); }
template <> const double& Value::get<double>() const { return std::get<double>(m_data); }
template <> const Decimal& Value::get<Decimal>() const { return std::get<Decimal>(m_data); }
template <> const std::string& Value::get<std::string>() const { return std::get<std::string>(m_data); }
template <> const std::vector<std::uint8_t>& Value::get<std::vector<std::uint8_t>>() const { return std::get<std::vector<std::uint8_t>>(m_data); }
template <> const Date& Value::get<Date>() const { return std::get<Date>(m_data); }
template <> const Time& Value::get<Time>() const { return std::get<Time>(m_data); }
template <> const Timestamp& Value::get<Timestamp>() const { return std::get<Timestamp>(m_data); }
template <> const Interval& Value::get<Interval>() const { return std::get<Interval>(m_data); }
template <> const UUID& Value::get<UUID>() const { return std::get<UUID>(m_data); }
template <> const Json& Value::get<Json>() const { return std::get<Json>(m_data); }
template <> const Array& Value::get<Array>() const { return *std::get<std::shared_ptr<Array>>(m_data); }
template <> const Dict& Value::get<Dict>() const { return *std::get<std::shared_ptr<Dict>>(m_data); }

bool Value::operator==(const Value& other) const {
    if (m_is_null && other.m_is_null) return true;
    if (m_is_null || other.m_is_null) return false; // One is null, the other isn't
    if (m_type != other.m_type) return false;
    
    // For V1, simple std::variant == handles most of it, except for custom types inside variant
    // that define == appropriately, which we have done.
    if (m_type == TypeID::Array) {
        return get<Array>() == other.get<Array>();
    }
    if (m_type == TypeID::DICT) {
        return get<Dict>() == other.get<Dict>();
    }
    
    return m_data == other.m_data;
}

bool Value::operator<(const Value& other) const {
    if (m_is_null && other.m_is_null) return false;
    if (m_is_null) return true; // nulls first
    if (other.m_is_null) return false;
    
    if (m_type != other.m_type) return static_cast<std::uint16_t>(m_type) < static_cast<std::uint16_t>(other.m_type);
    
    if (m_type == TypeID::Array || m_type == TypeID::DICT) {
        return false; // Arrays and Dicts not strictly ordered in V1
    }
    
    return m_data < other.m_data;
}

std::string Value::to_string() const {
    if (m_is_null) return "NULL";
    
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>) return "NULL";
        else if constexpr (std::is_same_v<T, bool>) return arg ? "true" : "false";
        else if constexpr (std::is_arithmetic_v<T>) return std::to_string(arg);
        else if constexpr (std::is_same_v<T, std::string>) return arg;
        else if constexpr (std::is_same_v<T, std::vector<std::uint8_t>>) {
            // Hex format
            std::stringstream ss;
            ss << "\\x";
            for (auto b : arg) ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
            return ss.str();
        }
        else if constexpr (std::is_same_v<T, Decimal> || std::is_same_v<T, Date> || 
                           std::is_same_v<T, Time> || std::is_same_v<T, Timestamp> ||
                           std::is_same_v<T, Interval> || std::is_same_v<T, UUID> ||
                           std::is_same_v<T, Json>) {
            return arg.to_string();
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<Array>>) {
            return arg->to_string();
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<Dict>>) {
            return arg->to_string();
        }
        else if constexpr (std::is_same_v<T, std::pair<std::uint32_t, std::string>>) {
            return arg.second;
        }
        else return "UNKNOWN";
    }, m_data);
}

} // namespace nemo::types
