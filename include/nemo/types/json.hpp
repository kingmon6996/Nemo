#ifndef NEMO_TYPES_JSON_HPP
#define NEMO_TYPES_JSON_HPP

#include <string>
#include <vector>
#include <map>

namespace nemo::types {

class Value; // Forward declare

// Foundation for JSON representation.
class JsonNode {
public:
    enum class Type {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object
    };

    JsonNode() noexcept = default;
    explicit JsonNode(bool val) : m_type(Type::Boolean), m_bool(val) {}
    explicit JsonNode(double val) : m_type(Type::Number), m_num(val) {}
    explicit JsonNode(std::string val) : m_type(Type::String), m_str(std::move(val)) {}
    explicit JsonNode(std::vector<JsonNode> arr) : m_type(Type::Array), m_arr(std::move(arr)) {}
    explicit JsonNode(std::map<std::string, JsonNode> obj) : m_type(Type::Object), m_obj(std::move(obj)) {}

    Type type() const noexcept { return m_type; }

    bool get_boolean() const { return m_bool; }
    double get_number() const { return m_num; }
    const std::string& get_string() const { return m_str; }
    const std::vector<JsonNode>& get_array() const { return m_arr; }
    const std::map<std::string, JsonNode>& get_object() const { return m_obj; }

    std::vector<JsonNode>& get_array_mut() { return m_arr; }
    std::map<std::string, JsonNode>& get_object_mut() { return m_obj; }

    std::string to_string() const;

private:
    Type m_type{Type::Null};
    
    bool m_bool{false};
    double m_num{0.0};
    std::string m_str;
    std::vector<JsonNode> m_arr;
    std::map<std::string, JsonNode> m_obj;
};

// Represents a JSON value
class Json {
public:
    Json() = default;
    explicit Json(JsonNode root) : m_root(std::move(root)) {}
    
    static Json parse(const std::string& str);
    std::string serialize() const { return to_string(); }

    const JsonNode& root() const { return m_root; }
    std::string to_string() const { return m_root.to_string(); }

    std::string type() const;
    
    Json get(const std::string& path) const;
    void set(const std::string& path, const Json& value);
    bool remove(const std::string& path);
    bool contains(const std::string& path) const;
    
    std::size_t array_length() const;
    std::vector<std::string> keys() const;

    bool operator==(const Json& other) const;
    bool operator<(const Json& other) const;

private:
    JsonNode m_root;
    JsonNode* resolve_path(const std::string& path) const; // Returns const pointer, will cast away const internally for mutations, or we can use separate mut method.
    JsonNode* resolve_path_mut(const std::string& path);
};

} // namespace nemo::types

#endif // NEMO_TYPES_JSON_HPP
