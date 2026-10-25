#include "nemo/types/json.hpp"
#include <sstream>

namespace nemo::types {

std::string JsonNode::to_string() const {
    switch (m_type) {
        case Type::Null: return "null";
        case Type::Boolean: return m_bool ? "true" : "false";
        case Type::Number: return std::to_string(m_num);
        case Type::String: return "\"" + m_str + "\"";
        case Type::Array: {
            std::string res = "[";
            for (size_t i = 0; i < m_arr.size(); ++i) {
                res += m_arr[i].to_string();
                if (i + 1 < m_arr.size()) res += ",";
            }
            return res + "]";
        }
        case Type::Object: {
            std::string res = "{";
            bool first = true;
            for (const auto& [k, v] : m_obj) {
                if (!first) res += ",";
                res += "\"" + k + "\":" + v.to_string();
                first = false;
            }
            return res + "}";
        }
    }
    return "null";
}

bool operator==(const JsonNode& lhs, const JsonNode& rhs) {
    if (lhs.type() != rhs.type()) return false;
    switch (lhs.type()) {
        case JsonNode::Type::Null: return true;
        case JsonNode::Type::Boolean: return lhs.get_boolean() == rhs.get_boolean();
        case JsonNode::Type::Number: return lhs.get_number() == rhs.get_number();
        case JsonNode::Type::String: return lhs.get_string() == rhs.get_string();
        case JsonNode::Type::Array: return lhs.get_array() == rhs.get_array();
        case JsonNode::Type::Object: return lhs.get_object() == rhs.get_object();
    }
    return false;
}

Json Json::parse(const std::string& str) {
    // Placeholder for actual parsing logic.
    // For Module 4, we just treat unparsed JSON as a string node wrapper.
    return Json(JsonNode(str));

}

bool Json::operator==(const Json& other) const {
    return m_root.to_string() == other.m_root.to_string(); // Inefficient but works for now
}

bool Json::operator<(const Json& other) const {
    return m_root.to_string() < other.m_root.to_string(); // Inefficient but works for now
}

std::string Json::type() const {
    switch (m_root.type()) {
        case JsonNode::Type::Null: return "null";
        case JsonNode::Type::Boolean: return "boolean";
        case JsonNode::Type::Number: return "number";
        case JsonNode::Type::String: return "string";
        case JsonNode::Type::Array: return "array";
        case JsonNode::Type::Object: return "object";
    }
    return "null";
}

// Very basic, hacky path resolver for $.key and $[index]
JsonNode* Json::resolve_path_mut(const std::string& path) {
    if (path.empty() || path == "$") return &m_root;
    
    JsonNode* current = &m_root;
    size_t pos = 1; // skip '$'
    
    while (pos < path.length() && current) {
        if (path[pos] == '.') {
            pos++;
            size_t end = path.find_first_of(".[", pos);
            if (end == std::string::npos) end = path.length();
            std::string key = path.substr(pos, end - pos);
            pos = end;
            
            if (current->type() != JsonNode::Type::Object) return nullptr;
            auto& obj = current->get_object_mut();
            auto it = obj.find(key);
            if (it == obj.end()) return nullptr;
            current = &it->second;
        } else if (path[pos] == '[') {
            pos++;
            size_t end = path.find(']', pos);
            if (end == std::string::npos) return nullptr;
            std::string idx_str = path.substr(pos, end - pos);
            pos = end + 1;
            
            if (current->type() != JsonNode::Type::Array) return nullptr;
            auto& arr = current->get_array_mut();
            try {
                size_t idx = std::stoull(idx_str);
                if (idx >= arr.size()) return nullptr;
                current = &arr[idx];
            } catch (...) {
                return nullptr;
            }
        } else {
            return nullptr; // Malformed
        }
    }
    return current;
}

JsonNode* Json::resolve_path(const std::string& path) const {
    // Cast away constness to reuse the logic. It's safe since we only return it or read from it.
    return const_cast<Json*>(this)->resolve_path_mut(path);
}

Json Json::get(const std::string& path) const {
    JsonNode* node = resolve_path(path);
    if (!node) return Json(JsonNode()); // SQL NULL or JSON Null? Returning JSON Null
    return Json(*node);
}

void Json::set(const std::string& path, const Json& value) {
    // Minimal implementation: if the path resolves to an existing node, overwrite it.
    // Ideally it would create objects/arrays on the fly, but that's complex.
    JsonNode* node = resolve_path_mut(path);
    if (node) {
        *node = value.root();
    }
}

bool Json::remove(const std::string& /*path*/) {
    // Simple remove: requires parent. Not implementing full parent resolution for now.
    // Returning false if unsupported.
    return false; 
}

bool Json::contains(const std::string& path) const {
    return resolve_path(path) != nullptr;
}

std::size_t Json::array_length() const {
    if (m_root.type() == JsonNode::Type::Array) {
        return m_root.get_array().size();
    }
    return 0;
}

std::vector<std::string> Json::keys() const {
    std::vector<std::string> ks;
    if (m_root.type() == JsonNode::Type::Object) {
        for (const auto& [k, v] : m_root.get_object()) {
            ks.push_back(k);
        }
    }
    return ks;
}

} // namespace nemo::types
