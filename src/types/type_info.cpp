#include "nemo/types/type_info.hpp"
#include <array>
#include <stdexcept>

namespace nemo::types {

namespace {

constexpr std::array<TypeInfo, static_cast<std::size_t>(TypeID::Invalid) + 1> TYPE_INFOS = {{
    { TypeID::Null,        "NULL",        0,  false, false, false, false, false, false, false },
    { TypeID::Boolean,     "BOOLEAN",     1,  false, true,  true,  true,  false, false, false },
    { TypeID::SmallInt,    "SMALLINT",    2,  false, true,  true,  true,  true,  false, false },
    { TypeID::Integer,     "INTEGER",     4,  false, true,  true,  true,  true,  false, false },
    { TypeID::BigInt,      "BIGINT",      8,  false, true,  true,  true,  true,  false, false },
    { TypeID::Real,        "REAL",        4,  false, true,  true,  true,  true,  false, false },
    { TypeID::Double,      "DOUBLE",      8,  false, true,  true,  true,  true,  false, false },
    { TypeID::Numeric,     "NUMERIC",     0,  true,  true,  true,  true,  true,  false, false }, // Variable
    { TypeID::Char,        "CHAR",        0,  true,  true,  true,  true,  false, false, false }, // Var but bounded
    { TypeID::VarChar,     "VARCHAR",     0,  true,  true,  true,  true,  false, false, false },
    { TypeID::Text,        "TEXT",        0,  true,  true,  true,  true,  false, false, false },
    { TypeID::Bytea,       "BYTEA",       0,  true,  true,  true,  true,  false, false, false },
    { TypeID::Date,        "DATE",        4,  false, true,  true,  true,  false, false, false },
    { TypeID::Time,        "TIME",        8,  false, true,  true,  true,  false, false, false },
    { TypeID::Timestamp,   "TIMESTAMP",   8,  false, true,  true,  true,  false, false, false },
    { TypeID::TimestampTZ, "TIMESTAMPTZ", 8,  false, true,  true,  true,  false, false, false },
    { TypeID::Interval,    "INTERVAL",    16, false, true,  true,  false, false, false, false },
    { TypeID::UUID,        "UUID",        16, false, true,  true,  true,  false, false, false },
    { TypeID::JSON,        "JSON",        0,  true,  false, false, false, false, false, true  },
    { TypeID::DICT,        "DICT",        0,  true,  true,  true,  true,  false, false, true  },
    { TypeID::Array,       "ARRAY",       0,  true,  true,  true,  true,  false, true,  true  },
    { TypeID::Enum,        "ENUM",        4,  false, true,  true,  true,  false, false, true  },
    { TypeID::Invalid,     "INVALID",     0,  false, false, false, false, false, false, false }
}};

} // namespace

const TypeInfo& get_type_info(TypeID id) noexcept {
    auto index = static_cast<std::size_t>(id);
    if (index >= TYPE_INFOS.size()) {
        return TYPE_INFOS.back(); // Invalid
    }
    return TYPE_INFOS[index];
}

} // namespace nemo::types
