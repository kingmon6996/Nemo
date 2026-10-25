#ifndef NEMO_TYPES_VALUE_HPP
#define NEMO_TYPES_VALUE_HPP

#include "nemo/types/type_id.hpp"
#include "nemo/types/decimal.hpp"
#include "nemo/types/uuid.hpp"
#include "nemo/types/temporal.hpp"
#include "nemo/types/json.hpp"
#include <string>
#include <vector>
#include <variant>
#include <memory>
#include <cstdint>

namespace nemo::types {

class Array; // Forward declare Array
class Dict;  // ADD THIS LINE

// The central Value representation for Nemo.
class Value {
public:
    Value() noexcept; // Creates a NULL of TypeID::Null
    explicit Value(TypeID type) noexcept; // Creates a NULL of specific type

    // Primitive constructors
    explicit Value(bool val) noexcept;
    explicit Value(std::int16_t val) noexcept;
    explicit Value(std::int32_t val) noexcept;
    explicit Value(std::int64_t val) noexcept;
    explicit Value(float val) noexcept;
    explicit Value(double val) noexcept;
    
    // Complex constructors
    explicit Value(Decimal val) noexcept;
    explicit Value(std::string val, TypeID type = TypeID::Text); // CHAR, VARCHAR, TEXT
    explicit Value(std::vector<std::uint8_t> val); // BYTEA
    explicit Value(Date val) noexcept;
    explicit Value(Time val) noexcept;
    explicit Value(Timestamp val, bool with_tz = false) noexcept;
    explicit Value(Interval val) noexcept;
    explicit Value(UUID val) noexcept;
    explicit Value(Json val);
    explicit Value(Array val);
    explicit Value(Dict val);
    
    // Enum (just storing label as string + ID for now)
    Value(std::uint32_t enum_id, std::string label);

    Value(const Value& other);
    Value& operator=(const Value& other);
    Value(Value&& other) noexcept;
    Value& operator=(Value&& other) noexcept;
    ~Value();

    TypeID type() const noexcept { return m_type; }
    bool is_null() const noexcept { return m_is_null; }

    // Accessors
    template <typename T>
    const T& get() const;

    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const { return !(*this == other); }
    bool operator<(const Value& other) const;

    std::string to_string() const;

private:
    TypeID m_type;
    bool m_is_null;
    
    // To keep Value size reasonable, we use a custom tagged union or std::variant.
    // std::variant can get quite large. For V1 we use std::variant.
    // In production, we'd manually manage a union to pack it perfectly.
    using VariantType = std::variant<
        std::monostate,
        bool,
        std::int16_t,
        std::int32_t,
        std::int64_t,
        float,
        double,
        Decimal,
        std::string,
        std::vector<std::uint8_t>,
        Date,
        Time,
        Timestamp,
        Interval,
        UUID,
        Json,
        std::shared_ptr<Array>, // Using shared_ptr to avoid incomplete type recursive pain
        std::shared_ptr<Dict>,
        std::pair<std::uint32_t, std::string> // Enum
    >;

    VariantType m_data;
};

// Template specializations
template <> const bool& Value::get<bool>() const;
template <> const std::int16_t& Value::get<std::int16_t>() const;
template <> const std::int32_t& Value::get<std::int32_t>() const;
template <> const std::int64_t& Value::get<std::int64_t>() const;
template <> const float& Value::get<float>() const;
template <> const double& Value::get<double>() const;
template <> const Decimal& Value::get<Decimal>() const;
template <> const std::string& Value::get<std::string>() const;
template <> const std::vector<std::uint8_t>& Value::get<std::vector<std::uint8_t>>() const;
template <> const Date& Value::get<Date>() const;
template <> const Time& Value::get<Time>() const;
template <> const Timestamp& Value::get<Timestamp>() const;
template <> const Interval& Value::get<Interval>() const;
template <> const UUID& Value::get<UUID>() const;
template <> const Json& Value::get<Json>() const;
template <> const Array& Value::get<Array>() const;
template <> const Dict& Value::get<Dict>() const;

} // namespace nemo::types

#endif // NEMO_TYPES_VALUE_HPP
