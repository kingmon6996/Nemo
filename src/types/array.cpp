#include "nemo/types/array.hpp"
#include "nemo/types/value.hpp"
#include <stdexcept>
#include <algorithm>

namespace nemo::types {

struct Array::Impl {
    std::vector<Value> elements;
};

Array::Array(TypeID element_type) : m_element_type(element_type), m_impl(new Impl()) {}

Array::Array(const Array& other) : m_element_type(other.m_element_type), m_impl(new Impl(*other.m_impl)) {}

Array& Array::operator=(const Array& other) {
    if (this != &other) {
        m_element_type = other.m_element_type;
        *m_impl = *other.m_impl;
    }
    return *this;
}

Array::Array(Array&& other) noexcept : m_element_type(other.m_element_type), m_impl(other.m_impl) {
    other.m_impl = nullptr;
}

Array& Array::operator=(Array&& other) noexcept {
    if (this != &other) {
        delete m_impl;
        m_element_type = other.m_element_type;
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

Array::~Array() {
    delete m_impl;
}

std::size_t Array::size() const noexcept {
    return m_impl ? m_impl->elements.size() : 0;
}

bool Array::empty() const noexcept {
    return m_impl ? m_impl->elements.empty() : true;
}

void Array::push_back(const Value& val) {
    if (m_impl) m_impl->elements.push_back(val);
}

void Array::append(const Value& val) {
    push_back(val);
}

void Array::prepend(const Value& val) {
    if (m_impl) {
        m_impl->elements.insert(m_impl->elements.begin(), val);
    }
}

const Value& Array::at(std::size_t index) const {
    if (!m_impl) throw std::out_of_range("Array is empty");
    return m_impl->elements.at(index);
}

const Value& Array::get(std::size_t index) const {
    return at(index);
}

void Array::set(std::size_t index, const Value& value) {
    if (!m_impl) throw std::out_of_range("Array is empty");
    m_impl->elements.at(index) = value;
}

Value Array::pop() {
    if (!m_impl || m_impl->elements.empty()) throw std::out_of_range("Array is empty");
    Value val = std::move(m_impl->elements.back());
    m_impl->elements.pop_back();
    return val;
}

void Array::remove(std::size_t index) {
    if (!m_impl || index >= m_impl->elements.size()) throw std::out_of_range("Index out of bounds");
    m_impl->elements.erase(m_impl->elements.begin() + static_cast<std::ptrdiff_t>(index));
}

void Array::clear() {
    if (m_impl) m_impl->elements.clear();
}

bool Array::contains(const Value& value) const {
    if (!m_impl) return false;
    for (const auto& el : m_impl->elements) {
        if (el == value) return true;
    }
    return false;
}

Array Array::slice(std::size_t start, std::size_t end) const {
    Array res(m_element_type);
    if (!m_impl || start >= m_impl->elements.size()) return res;
    
    std::size_t actual_end = std::min(end, m_impl->elements.size());
    if (start >= actual_end) return res;
    
    res.m_impl->elements.reserve(actual_end - start);
    for (std::size_t i = start; i < actual_end; ++i) {
        res.push_back(m_impl->elements[i]);
    }
    return res;
}

bool Array::operator==(const Array& other) const {
    if (!m_impl && !other.m_impl) return true;
    if (!m_impl || !other.m_impl) return false;
    
    if (m_element_type != other.m_element_type) return false;
    if (size() != other.size()) return false;
    
    for (std::size_t i = 0; i < size(); ++i) {
        if (at(i) != other.at(i)) return false;
    }
    return true;
}

std::string Array::to_string() const {
    std::string res = "[";
    for (std::size_t i = 0; i < size(); ++i) {
        res += at(i).to_string();
        if (i + 1 < size()) res += ", ";
    }
    res += "]";
    return res;
}

} // namespace nemo::types
