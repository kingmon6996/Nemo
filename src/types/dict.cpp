#include "nemo/types/dict.hpp"
#include "nemo/types/value.hpp"
#include <unordered_map>
#include <stdexcept>
#include <sstream>
#include <algorithm>

namespace nemo::types {

struct Dict::Impl {
    std::unordered_map<std::string, Value> map;
};

Dict::Dict() : m_impl(new Impl()) {}

Dict::~Dict() {
    delete m_impl;
}

Dict::Dict(const Dict& other) : m_impl(new Impl(*other.m_impl)) {}

Dict& Dict::operator=(const Dict& other) {
    if (this != &other) {
        *m_impl = *other.m_impl;
    }
    return *this;
}

Dict::Dict(Dict&& other) noexcept : m_impl(other.m_impl) {
    other.m_impl = nullptr;
}

Dict& Dict::operator=(Dict&& other) noexcept {
    if (this != &other) {
        delete m_impl;
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

std::size_t Dict::size() const noexcept {
    return m_impl ? m_impl->map.size() : 0;
}

bool Dict::empty() const noexcept {
    return m_impl ? m_impl->map.empty() : true;
}

bool Dict::contains(const std::string& key) const {
    if (!m_impl) return false;
    return m_impl->map.find(key) != m_impl->map.end();
}

const Value* Dict::get(const std::string& key) const {
    if (!m_impl) return nullptr;
    auto it = m_impl->map.find(key);
    if (it != m_impl->map.end()) {
        return &it->second;
    }
    return nullptr;
}

void Dict::set(const std::string& key, const Value& value) {
    if (!m_impl) m_impl = new Impl();
    m_impl->map[key] = value;
}

bool Dict::remove(const std::string& key) {
    if (!m_impl) return false;
    return m_impl->map.erase(key) > 0;
}

void Dict::clear() {
    if (m_impl) {
        m_impl->map.clear();
    }
}

std::vector<std::string> Dict::keys() const {
    std::vector<std::string> ks;
    if (m_impl) {
        ks.reserve(m_impl->map.size());
        for (const auto& [k, v] : m_impl->map) {
            ks.push_back(k);
        }
    }
    return ks;
}

bool Dict::operator==(const Dict& other) const {
    if (!m_impl && !other.m_impl) return true;
    if (!m_impl || !other.m_impl) return false; // One is empty, other isn't, except if empty maps
    if (m_impl->map.size() != other.m_impl->map.size()) return false;
    
    for (const auto& [k, v] : m_impl->map) {
        auto it = other.m_impl->map.find(k);
        if (it == other.m_impl->map.end()) return false;
        if (v != it->second) return false;
    }
    return true;
}

std::string Dict::to_string() const {
    if (!m_impl || m_impl->map.empty()) return "{}";
    
    std::vector<std::string> sorted_keys = keys();
    std::sort(sorted_keys.begin(), sorted_keys.end());
    
    std::ostringstream oss;
    oss << "{";
    for (size_t i = 0; i < sorted_keys.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << "\"" << sorted_keys[i] << "\": " << m_impl->map.at(sorted_keys[i]).to_string();
    }
    oss << "}";
    return oss.str();
}

} // namespace nemo::types
