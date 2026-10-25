#include "nemo/catalog/default_value.hpp"
#include <stdexcept>

namespace nemo::catalog {

DefaultValue::DefaultValue(types::Value val) 
    : m_has_value(true), m_value(std::move(val)) {}

const types::Value& DefaultValue::value() const {
    if (!m_has_value) {
        throw std::runtime_error("No default value present");
    }
    return m_value;
}

} // namespace nemo::catalog
