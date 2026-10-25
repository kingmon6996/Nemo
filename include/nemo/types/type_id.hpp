#ifndef NEMO_TYPES_TYPE_ID_HPP
#define NEMO_TYPES_TYPE_ID_HPP

#include <cstdint>

namespace nemo::types {

enum class TypeID : std::uint16_t {
    Null = 0,

    Boolean,

    SmallInt,
    Integer,
    BigInt,

    Real,
    Double,

    Numeric, // Aliased to Decimal
    Decimal = Numeric,

    Char,
    VarChar,
    Text,

    Bytea,

    Date,
    Time,
    Timestamp,
    TimestampTZ,
    Interval,

    UUID,

    JSON,
    JSONB,
    DICT,

    Array,
    Enum,

    Bit,
    VarBit,
    Inet,
    Cidr,
    MacAddr,
    Range,

    // Sentinel
    Invalid
};

} // namespace nemo::types

#endif // NEMO_TYPES_TYPE_ID_HPP
