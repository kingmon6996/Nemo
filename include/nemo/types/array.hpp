#ifndef NEMO_TYPES_ARRAY_HPP
#define NEMO_TYPES_ARRAY_HPP

#include "nemo/types/type_id.hpp"
#include <vector>
#include <string>

namespace nemo::types {

class Value; // Forward declare

// A structured array that knows its element type.
class Array {
public:
    Array(TypeID element_type = TypeID::Invalid);
    ~Array();

    Array(const Array& other);
    Array& operator=(const Array& other);
    Array(Array&& other) noexcept;
    Array& operator=(Array&& other) noexcept;

    TypeID element_type() const noexcept { return m_element_type; }
    std::size_t size() const noexcept;
    bool empty() const noexcept;
    
    void push_back(const Value& val);
    void append(const Value& val);
    void prepend(const Value& val);
    
    const Value& at(std::size_t index) const;
    const Value& get(std::size_t index) const;
    void set(std::size_t index, const Value& value);
    
    Value pop();
    void remove(std::size_t index);
    void clear();
    
    bool contains(const Value& value) const;
    Array slice(std::size_t start, std::size_t end) const;

    bool operator==(const Array& other) const;

    std::string to_string() const;

private:
    TypeID m_element_type;
    struct Impl;
    Impl* m_impl;
};

} // namespace nemo::types

#endif // NEMO_TYPES_ARRAY_HPP
