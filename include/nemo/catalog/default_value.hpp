#ifndef NEMO_CATALOG_DEFAULT_VALUE_HPP
#define NEMO_CATALOG_DEFAULT_VALUE_HPP

#include "nemo/types/value.hpp"

namespace nemo::catalog {

// A DefaultValue for a column. Currently supports constant Values.
class DefaultValue {
public:
    DefaultValue() = default; // Represents no default value
    explicit DefaultValue(types::Value val);

    bool has_value() const noexcept { return m_has_value; }
    const types::Value& value() const;

private:
    bool m_has_value{false};
    types::Value m_value;
};

} // namespace nemo::catalog

#endif // NEMO_CATALOG_DEFAULT_VALUE_HPP
