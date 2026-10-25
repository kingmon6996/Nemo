#ifndef NEMO_SRC_API_FUNCTION_REGISTRY_HPP
#define NEMO_SRC_API_FUNCTION_REGISTRY_HPP

#include "nemo/types/value.hpp"
#include "nemo/types/type_id.hpp"
#include "nemo/types/type_cast.hpp"
#include "nemo/types/type_registry.hpp"
#include "nemo/types/uuid.hpp"
#include "nemo/types/array.hpp"
#include "nemo/types/dict.hpp"
#include "api_internal.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <chrono>
#include <random>
#include <functional>

namespace nemo::api::functions {

inline std::string trim(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return std::string(s.substr(start, end - start));
}

inline std::string to_lower(std::string_view s) {
    std::string res;
    res.reserve(s.size());
    for (char c : s) res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return res;
}

inline std::string to_upper(std::string_view s) {
    std::string res;
    res.reserve(s.size());
    for (char c : s) res.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return res;
}

// =============================================================================
// JSON / JSONB PATH EXTRACTION UTILITIES
// =============================================================================

inline std::string extract_json_path(std::string_view json, std::string_view path) {
    if (path.starts_with("$.")) path.remove_prefix(2);
    else if (path.starts_with(".")) path.remove_prefix(1);
    else if (path == "$") return std::string(json);

    // Support nested dot-separated paths (e.g. "user.city")
    size_t dot = path.find('.');
    if (dot != std::string_view::npos) {
        std::string_view first_part = path.substr(0, dot);
        std::string_view remaining = path.substr(dot + 1);
        std::string intermediate = extract_json_path(json, first_part);
        if (intermediate.empty()) return "";
        return extract_json_path(intermediate, remaining);
    }

    std::string current_key;
    int array_index = -1;

    size_t bracket_open = path.find('[');
    if (bracket_open != std::string_view::npos) {
        size_t bracket_close = path.find(']', bracket_open);
        if (bracket_close != std::string_view::npos) {
            current_key = std::string(path.substr(0, bracket_open));
            try {
                array_index = std::stoi(std::string(path.substr(bracket_open + 1, bracket_close - (bracket_open + 1))));
            } catch (...) {}
        }
    } else {
        current_key = std::string(path);
    }

    if (current_key.empty() && array_index >= 0) {
        size_t arr_start = json.find('[');
        if (arr_start == std::string_view::npos) return "";
        size_t p = arr_start + 1;
        int idx = 0;
        while (p < json.size()) {
            while (p < json.size() && (std::isspace(static_cast<unsigned char>(json[p])) || json[p] == ',')) p++;
            if (p >= json.size() || json[p] == ']') break;
            size_t val_start = p;
            if (json[p] == '"' || json[p] == '\'') {
                char q = json[p++];
                while (p < json.size() && json[p] != q) p++;
                if (p < json.size()) p++;
            } else if (json[p] == '{' || json[p] == '[') {
                char open_c = json[p];
                char close_c = (open_c == '{') ? '}' : ']';
                int depth = 1;
                p++;
                while (p < json.size() && depth > 0) {
                    if (json[p] == open_c) depth++;
                    else if (json[p] == close_c) depth--;
                    p++;
                }
            } else {
                while (p < json.size() && json[p] != ',' && json[p] != ']' && !std::isspace(static_cast<unsigned char>(json[p]))) p++;
            }
            if (idx == array_index) {
                std::string res = std::string(json.substr(val_start, p - val_start));
                if (res.size() >= 2 && ((res.front() == '"' && res.back() == '"') || (res.front() == '\'' && res.back() == '\''))) {
                    return res.substr(1, res.size() - 2);
                }
                return res;
            }
            idx++;
        }
        return "";
    }

    std::string needle = "\"" + current_key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string_view::npos) {
        needle = "'" + current_key + "'";
        pos = json.find(needle);
    }
    if (pos == std::string_view::npos) return "";

    pos += needle.size();
    while (pos < json.size() && (std::isspace(static_cast<unsigned char>(json[pos])) || json[pos] == ':')) pos++;
    if (pos >= json.size()) return "";

    if (array_index >= 0 && json[pos] == '[') {
        size_t arr_end = json.find(']', pos);
        if (arr_end != std::string_view::npos) {
            std::string sub_arr = std::string(json.substr(pos, arr_end - pos + 1));
            return extract_json_path(sub_arr, "$[" + std::to_string(array_index) + "]");
        }
    }

    if (json[pos] == '"' || json[pos] == '\'') {
        char quote = json[pos++];
        size_t end = json.find(quote, pos);
        if (end != std::string_view::npos) return std::string(json.substr(pos, end - pos));
        return "";
    }

    if (json[pos] == '{' || json[pos] == '[') {
        char open_c = json[pos];
        char close_c = (open_c == '{') ? '}' : ']';
        int depth = 1;
        size_t p = pos + 1;
        while (p < json.size() && depth > 0) {
            if (json[p] == open_c) depth++;
            else if (json[p] == close_c) depth--;
            p++;
        }
        return std::string(json.substr(pos, p - pos));
    }

    size_t end = pos;
    while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ']' && !std::isspace(static_cast<unsigned char>(json[end]))) {
        end++;
    }
    return std::string(json.substr(pos, end - pos));
}

inline types::Value parse_json_typed(const std::string& val_str) {
    if (val_str.empty()) return types::Value();
    if (val_str == "true" || val_str == "TRUE") return types::Value(true);
    if (val_str == "false" || val_str == "FALSE") return types::Value(false);
    if (val_str == "null" || val_str == "NULL") return types::Value();

    if (val_str.size() >= 2 && ((val_str.front() == '"' && val_str.back() == '"') || (val_str.front() == '\'' && val_str.back() == '\''))) {
        return types::Value(val_str.substr(1, val_str.size() - 2), types::TypeID::Text);
    }

    bool has_dot = (val_str.find('.') != std::string::npos);
    if (has_dot) {
        try { return types::Value(static_cast<double>(std::stod(val_str))); } catch (...) {}
    } else {
        try { return types::Value(static_cast<std::int64_t>(std::stoll(val_str))); } catch (...) {}
    }
    return types::Value(val_str, types::TypeID::Text);
}

inline std::string unquote_string(std::string_view s) {
    std::string_view t = trim(s);
    if (t.size() >= 2 && ((t.front() == '"' && t.back() == '"') || (t.front() == '\'' && t.back() == '\''))) {
        return std::string(t.substr(1, t.size() - 2));
    }
    return std::string(t);
}

// Helper to parse comma-separated array string: "[1, 2, 3]" or "{1, 2, 3}"
inline std::vector<std::string> parse_array_elements(std::string_view s) {
    std::vector<std::string> elements;
    while (!s.empty() && (s.front() == '[' || s.front() == '{' || std::isspace(static_cast<unsigned char>(s.front())))) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ']' || s.back() == '}' || std::isspace(static_cast<unsigned char>(s.back())))) s.remove_suffix(1);
    
    std::string curr;
    bool in_q = false;
    char q_char = '\0';
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (!in_q && (c == '\'' || c == '"')) {
            in_q = true;
            q_char = c;
        } else if (in_q && c == q_char) {
            in_q = false;
        } else if (!in_q && c == ',') {
            std::string t = trim(curr);
            if (!t.empty()) elements.push_back(t);
            curr.clear();
            continue;
        }
        curr += c;
    }
    std::string t = trim(curr);
    if (!t.empty()) elements.push_back(t);
    return elements;
}

// Helper to parse key-value pairs from dictionary string: '{"a": 1, "b": "val"}'
inline std::vector<std::pair<std::string, std::string>> parse_dict_entries(std::string_view s) {
    std::vector<std::pair<std::string, std::string>> entries;
    while (!s.empty() && (s.front() == '{' || std::isspace(static_cast<unsigned char>(s.front())))) s.remove_prefix(1);
    while (!s.empty() && (s.back() == '}' || std::isspace(static_cast<unsigned char>(s.back())))) s.remove_suffix(1);

    auto items = parse_array_elements(s);
    for (const auto& item : items) {
        size_t colon = item.find(':');
        if (colon != std::string::npos) {
            std::string k = trim(item.substr(0, colon));
            std::string v = trim(item.substr(colon + 1));
            if (k.size() >= 2 && ((k.front() == '"' && k.back() == '"') || (k.front() == '\'' && k.back() == '\''))) {
                k = k.substr(1, k.size() - 2);
            }
            if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\''))) {
                v = v.substr(1, v.size() - 2);
            }
            entries.emplace_back(std::move(k), std::move(v));
        }
    }
    return entries;
}

// Base64 lookup table
static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline std::string base64_encode(const std::vector<std::uint8_t>& data) {
    std::string ret;
    int i = 0, j = 0;
    std::uint8_t char_array_3[3], char_array_4[4];

    for (std::uint8_t b : data) {
        char_array_3[i++] = b;
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for (i = 0; i < 4; i++) ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    if (i) {
        for (j = i; j < 3; j++) char_array_3[j] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        for (j = 0; j < i + 1; j++) ret += base64_chars[char_array_4[j]];
        while (i++ < 3) ret += '=';
    }
    return ret;
}

inline std::vector<std::uint8_t> base64_decode(std::string_view const& encoded_string) {
    size_t in_len = encoded_string.size();
    int i = 0, j = 0, in_ = 0;
    std::uint8_t char_array_4[4], char_array_3[3];
    std::vector<std::uint8_t> ret;

    while (in_len-- && (encoded_string[in_] != '=') && (std::isalnum(static_cast<unsigned char>(encoded_string[in_])) || encoded_string[in_] == '+' || encoded_string[in_] == '/')) {
        char_array_4[i++] = encoded_string[in_]; in_++;
        if (i == 4) {
            for (i = 0; i < 4; i++) char_array_4[i] = static_cast<std::uint8_t>(base64_chars.find(char_array_4[i]));
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            for (i = 0; i < 3; i++) ret.push_back(char_array_3[i]);
            i = 0;
        }
    }
    if (i) {
        for (j = 0; j < i; j++) char_array_4[j] = static_cast<std::uint8_t>(base64_chars.find(char_array_4[j]));
        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        for (j = 0; j < i - 1; j++) ret.push_back(char_array_3[j]);
    }
    return ret;
}

// =============================================================================
// CENTRALIZED FUNCTION REGISTRY EVALUATION
// =============================================================================

class FunctionRegistry {
public:
    static types::Value evaluate(
        const std::string& raw_func_name,
        const std::vector<types::Value>& args,
        SharedDatabaseState* db_state = nullptr
    ) {
        std::string func = to_lower(trim(raw_func_name));

        // ---------------------------------------------------------------------
        // 1. SEQUENCE FUNCTIONS
        // ---------------------------------------------------------------------
        if (func == "nextval") {
            if (args.empty() || !db_state) return types::Value();
            std::string seq_name = args[0].to_string();
            auto seq = db_state->get_or_create_sequence(seq_name);
            int64_t val = seq->nextval();
            return types::Value(val);
        }
        if (func == "nextval_range") {
            if (args.empty() || !db_state) return types::Value();
            std::string seq_name = args[0].to_string();
            int64_t count = (args.size() > 1) ? std::stoll(args[1].to_string()) : 1;
            auto seq = db_state->get_or_create_sequence(seq_name);
            auto [start_id, end_id] = seq->nextval_range(count);
            // Return formatted range string: start_id..end_id
            return types::Value(std::to_string(start_id) + ".." + std::to_string(end_id), types::TypeID::Text);
        }
        if (func == "currval") {
            if (args.empty() || !db_state) return types::Value();
            std::string seq_name = args[0].to_string();
            auto seq = db_state->get_sequence(seq_name);
            if (!seq || !seq->is_called) throw std::runtime_error("Sequence '" + seq_name + "' not yet called");
            return types::Value(seq->current_val);
        }

        // ---------------------------------------------------------------------
        // 2. JSON & JSONB EXTRACTION FUNCTIONS
        // ---------------------------------------------------------------------
        if (func == "json_extract" || func == "json_value" || func == "json_get" || func == "jsonb_extract" || func == "jsonb_value") {
            if (args.size() < 2) return types::Value();
            std::string json_doc = args[0].to_string();
            std::string path = args[1].to_string();
            std::string extracted = extract_json_path(json_doc, path);
            return parse_json_typed(extracted);
        }
        if (func == "json_get_text") {
            if (args.size() < 2) return types::Value();
            return types::Value(extract_json_path(args[0].to_string(), args[1].to_string()), types::TypeID::Text);
        }
        if (func == "json_get_int") {
            if (args.size() < 2) return types::Value(static_cast<std::int64_t>(0));
            std::string ext = extract_json_path(args[0].to_string(), args[1].to_string());
            try { return types::Value(static_cast<std::int64_t>(std::stoll(ext))); } catch (...) { return types::Value(static_cast<std::int64_t>(0)); }
        }
        if (func == "json_get_float") {
            if (args.size() < 2) return types::Value(0.0);
            std::string ext = extract_json_path(args[0].to_string(), args[1].to_string());
            try { return types::Value(std::stod(ext)); } catch (...) { return types::Value(0.0); }
        }
        if (func == "json_get_bool") {
            if (args.size() < 2) return types::Value(false);
            std::string ext = extract_json_path(args[0].to_string(), args[1].to_string());
            return types::Value(ext == "true" || ext == "1");
        }
        if (func == "json_get_json") {
            if (args.size() < 2) return types::Value("{}", types::TypeID::JSON);
            return types::Value(extract_json_path(args[0].to_string(), args[1].to_string()), types::TypeID::JSON);
        }
        if (func == "json_exists") {
            if (args.size() < 2) return types::Value(false);
            std::string ext = extract_json_path(args[0].to_string(), args[1].to_string());
            return types::Value(!ext.empty());
        }
        if (func == "json_typeof") {
            if (args.empty() || args[0].is_null()) return types::Value("null", types::TypeID::Text);
            std::string s = trim(args[0].to_string());
            if (s.empty() || s == "null") return types::Value("null", types::TypeID::Text);
            if (s.front() == '{') return types::Value("object", types::TypeID::Text);
            if (s.front() == '[') return types::Value("array", types::TypeID::Text);
            if (s == "true" || s == "false") return types::Value("boolean", types::TypeID::Text);
            if (s.front() == '"' || s.front() == '\'') return types::Value("string", types::TypeID::Text);
            return types::Value("number", types::TypeID::Text);
        }
        if (func == "json_length" || func == "json_array_length") {
            if (args.empty()) return types::Value(static_cast<std::int64_t>(0));
            std::string s = trim(args[0].to_string());
            if (s.front() == '[') {
                return types::Value(static_cast<std::int64_t>(parse_array_elements(s).size()));
            }
            if (s.front() == '{') {
                return types::Value(static_cast<std::int64_t>(parse_dict_entries(s).size()));
            }
            return types::Value(static_cast<std::int64_t>(0));
        }
        if (func == "json_keys" || func == "json_object_keys") {
            if (args.empty()) return types::Value("[]", types::TypeID::Array);
            auto entries = parse_dict_entries(args[0].to_string());
            std::string res = "[";
            for (size_t i = 0; i < entries.size(); ++i) {
                if (i > 0) res += ", ";
                res += "\"" + entries[i].first + "\"";
            }
            res += "]";
            return types::Value(res, types::TypeID::Array);
        }
        if (func == "json_values") {
            if (args.empty()) return types::Value("[]", types::TypeID::Array);
            auto entries = parse_dict_entries(args[0].to_string());
            std::string res = "[";
            for (size_t i = 0; i < entries.size(); ++i) {
                if (i > 0) res += ", ";
                res += entries[i].second;
            }
            res += "]";
            return types::Value(res, types::TypeID::Array);
        }
        if (func == "json_contains" || func == "jsonb_contains") {
            if (args.size() < 2) return types::Value(false);
            std::string doc = args[0].to_string();
            std::string val = args[1].to_string();
            return types::Value(doc.find(val) != std::string::npos);
        }
        if (func == "json_merge" || func == "jsonb_merge") {
            if (args.size() < 2) return args.empty() ? types::Value() : args[0];
            auto e1 = parse_dict_entries(args[0].to_string());
            auto e2 = parse_dict_entries(args[1].to_string());
            std::unordered_map<std::string, std::string> merged;
            for (auto& p : e1) merged[p.first] = p.second;
            for (auto& p : e2) merged[p.first] = p.second;
            std::string out = "{";
            bool first = true;
            for (auto& p : merged) {
                if (!first) out += ", ";
                out += "\"" + p.first + "\": " + p.second;
                first = false;
            }
            out += "}";
            return types::Value(out, types::TypeID::JSON);
        }
        if (func == "json_array_get") {
            if (args.size() < 2) return types::Value();
            int idx = static_cast<int>(std::stoll(args[1].to_string()));
            auto elems = parse_array_elements(args[0].to_string());
            if (idx >= 0 && idx < static_cast<int>(elems.size())) {
                return parse_json_typed(elems[idx]);
            }
            return types::Value();
        }
        if (func == "json_set" || func == "jsonb_set") {
            if (args.size() < 3) return args.empty() ? types::Value() : args[0];
            auto entries = parse_dict_entries(args[0].to_string());
            std::string path = args[1].to_string();
            if (path.starts_with("$.") || path.starts_with("$.")) path = path.substr(2);
            else if (path.starts_with("$")) path = path.substr(1);
            std::string val = args[2].to_string();
            bool found = false;
            for (auto& e : entries) {
                if (e.first == path) {
                    e.second = val;
                    found = true;
                    break;
                }
            }
            if (!found) entries.emplace_back(path, val);
            std::string out = "{";
            for (size_t i = 0; i < entries.size(); ++i) {
                if (i > 0) out += ", ";
                out += "\"" + entries[i].first + "\": " + entries[i].second;
            }
            out += "}";
            return types::Value(out, func == "jsonb_set" ? types::TypeID::JSONB : types::TypeID::JSON);
        }
        if (func == "json_insert") {
            if (args.size() < 3) return args.empty() ? types::Value() : args[0];
            auto entries = parse_dict_entries(args[0].to_string());
            std::string path = args[1].to_string();
            if (path.starts_with("$.") || path.starts_with("$.")) path = path.substr(2);
            else if (path.starts_with("$")) path = path.substr(1);
            std::string val = args[2].to_string();
            bool found = false;
            for (const auto& e : entries) {
                if (e.first == path) {
                    found = true;
                    break;
                }
            }
            if (!found) entries.emplace_back(path, val);
            std::string out = "{";
            for (size_t i = 0; i < entries.size(); ++i) {
                if (i > 0) out += ", ";
                out += "\"" + entries[i].first + "\": " + entries[i].second;
            }
            out += "}";
            return types::Value(out, types::TypeID::JSON);
        }
        if (func == "json_remove") {
            if (args.size() < 2) return args.empty() ? types::Value() : args[0];
            auto entries = parse_dict_entries(args[0].to_string());
            std::string path = args[1].to_string();
            if (path.starts_with("$.") || path.starts_with("$.")) path = path.substr(2);
            else if (path.starts_with("$")) path = path.substr(1);
            std::string out = "{";
            bool first = true;
            for (const auto& e : entries) {
                if (e.first != path) {
                    if (!first) out += ", ";
                    out += "\"" + e.first + "\": " + e.second;
                    first = false;
                }
            }
            out += "}";
            return types::Value(out, types::TypeID::JSON);
        }
        if (func == "json_pretty") {
            if (args.empty()) return types::Value();
            return types::Value(args[0].to_string(), types::TypeID::JSON);
        }
        if (func == "json_quote") {
            if (args.empty()) return types::Value("\"\"", types::TypeID::Text);
            return types::Value("\"" + args[0].to_string() + "\"", types::TypeID::Text);
        }
        if (func == "json_unquote") {
            if (args.empty()) return types::Value("", types::TypeID::Text);
            std::string s = args[0].to_string();
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
                return types::Value(s.substr(1, s.size() - 2), types::TypeID::Text);
            }
            return types::Value(s, types::TypeID::Text);
        }

        // ---------------------------------------------------------------------
        // 3. ARRAY FUNCTIONS
        // ---------------------------------------------------------------------
        if (func == "array_length" || func == "array_cardinality") {
            if (args.empty() || args[0].is_null()) return types::Value();
            auto elems = parse_array_elements(args[0].to_string());
            return types::Value(static_cast<std::int64_t>(elems.size()));
        }
        if (func == "array_dims" || func == "array_ndims") {
            if (args.empty() || args[0].is_null()) return types::Value();
            if (func == "array_ndims") return types::Value(static_cast<std::int64_t>(1));
            auto elems = parse_array_elements(args[0].to_string());
            return types::Value("[1:" + std::to_string(elems.size()) + "]", types::TypeID::Text);
        }
        if (func == "array_lower") {
            if (args.empty() || args[0].is_null()) return types::Value();
            return types::Value(static_cast<std::int64_t>(1));
        }
        if (func == "array_upper") {
            if (args.empty() || args[0].is_null()) return types::Value();
            auto elems = parse_array_elements(args[0].to_string());
            return types::Value(static_cast<std::int64_t>(elems.size()));
        }
        if (func == "array_is_empty") {
            if (args.empty() || args[0].is_null()) return types::Value(true);
            auto elems = parse_array_elements(args[0].to_string());
            return types::Value(elems.empty());
        }
        if (func == "array_get") {
            // 1-based indexing for SQL array
            if (args.size() < 2) return types::Value();
            int64_t idx = std::stoll(args[1].to_string());
            auto elems = parse_array_elements(args[0].to_string());
            if (idx >= 1 && idx <= static_cast<int64_t>(elems.size())) {
                return parse_json_typed(elems[idx - 1]);
            }
            return types::Value();
        }
        if (func == "array_first") {
            if (args.empty() || args[0].is_null()) return types::Value();
            auto elems = parse_array_elements(args[0].to_string());
            return elems.empty() ? types::Value() : parse_json_typed(elems.front());
        }
        if (func == "array_last") {
            if (args.empty() || args[0].is_null()) return types::Value();
            auto elems = parse_array_elements(args[0].to_string());
            return elems.empty() ? types::Value() : parse_json_typed(elems.back());
        }
        if (func == "array_slice") {
            if (args.size() < 3) return types::Value("[]", types::TypeID::Array);
            int64_t start = std::stoll(args[1].to_string());
            int64_t end = std::stoll(args[2].to_string());
            auto elems = parse_array_elements(args[0].to_string());
            std::string out = "[";
            bool first = true;
            for (int64_t i = start; i <= end && i <= static_cast<int64_t>(elems.size()); ++i) {
                if (i >= 1) {
                    if (!first) out += ", ";
                    out += elems[i - 1];
                    first = false;
                }
            }
            out += "]";
            return types::Value(out, types::TypeID::Array);
        }
        if (func == "array_append") {
            if (args.size() < 2) return args.empty() ? types::Value() : args[0];
            auto elems = parse_array_elements(args[0].to_string());
            elems.push_back(args[1].to_string());
            std::string out = "[";
            for (size_t i = 0; i < elems.size(); ++i) {
                if (i > 0) out += ", ";
                out += elems[i];
            }
            out += "]";
            return types::Value(out, types::TypeID::Array);
        }
        if (func == "array_prepend") {
            if (args.size() < 2) return args.empty() ? types::Value() : args[0];
            auto elems = parse_array_elements(args[1].to_string());
            elems.insert(elems.begin(), args[0].to_string());
            std::string out = "[";
            for (size_t i = 0; i < elems.size(); ++i) {
                if (i > 0) out += ", ";
                out += elems[i];
            }
            out += "]";
            return types::Value(out, types::TypeID::Array);
        }
        if (func == "array_concat") {
            if (args.size() < 2) return args.empty() ? types::Value() : args[0];
            auto e1 = parse_array_elements(args[0].to_string());
            auto e2 = parse_array_elements(args[1].to_string());
            e1.insert(e1.end(), e2.begin(), e2.end());
            std::string out = "[";
            for (size_t i = 0; i < e1.size(); ++i) {
                if (i > 0) out += ", ";
                out += e1[i];
            }
            out += "]";
            return types::Value(out, types::TypeID::Array);
        }
        if (func == "array_remove") {
            if (args.size() < 2) return args.empty() ? types::Value() : args[0];
            auto elems = parse_array_elements(args[0].to_string());
            std::string target = args[1].to_string();
            std::string out = "[";
            bool first = true;
            for (const auto& e : elems) {
                if (e != target) {
                    if (!first) out += ", ";
                    out += e;
                    first = false;
                }
            }
            out += "]";
            return types::Value(out, types::TypeID::Array);
        }
        if (func == "array_replace") {
            if (args.size() < 3) return args.empty() ? types::Value() : args[0];
            auto elems = parse_array_elements(args[0].to_string());
            std::string old_v = args[1].to_string();
            std::string new_v = args[2].to_string();
            std::string out = "[";
            for (size_t i = 0; i < elems.size(); ++i) {
                if (i > 0) out += ", ";
                out += (elems[i] == old_v) ? new_v : elems[i];
            }
            out += "]";
            return types::Value(out, types::TypeID::Array);
        }
        if (func == "array_position") {
            if (args.size() < 2) return types::Value();
            auto elems = parse_array_elements(args[0].to_string());
            std::string target = unquote_string(args[1].to_string());
            for (size_t i = 0; i < elems.size(); ++i) {
                if (unquote_string(elems[i]) == target) return types::Value(static_cast<std::int64_t>(i + 1));
            }
            return types::Value();
        }
        if (func == "array_contains") {
            if (args.size() < 2) return types::Value(false);
            auto elems = parse_array_elements(args[0].to_string());
            std::string target = unquote_string(args[1].to_string());
            for (const auto& e : elems) {
                if (unquote_string(e) == target) return types::Value(true);
            }
            return types::Value(false);
        }
        if (func == "array_distinct") {
            if (args.empty()) return types::Value("[]", types::TypeID::Array);
            auto elems = parse_array_elements(args[0].to_string());
            std::vector<std::string> distinct;
            for (const auto& e : elems) {
                if (std::find(distinct.begin(), distinct.end(), e) == distinct.end()) {
                    distinct.push_back(e);
                }
            }
            std::string out = "[";
            for (size_t i = 0; i < distinct.size(); ++i) {
                if (i > 0) out += ", ";
                out += distinct[i];
            }
            out += "]";
            return types::Value(out, types::TypeID::Array);
        }
        if (func == "array_sort") {
            if (args.empty()) return types::Value("[]", types::TypeID::Array);
            auto elems = parse_array_elements(args[0].to_string());
            std::sort(elems.begin(), elems.end());
            std::string out = "[";
            for (size_t i = 0; i < elems.size(); ++i) {
                if (i > 0) out += ", ";
                out += elems[i];
            }
            out += "]";
            return types::Value(out, types::TypeID::Array);
        }
        if (func == "array_reverse") {
            if (args.empty()) return types::Value("[]", types::TypeID::Array);
            auto elems = parse_array_elements(args[0].to_string());
            std::reverse(elems.begin(), elems.end());
            std::string out = "[";
            for (size_t i = 0; i < elems.size(); ++i) {
                if (i > 0) out += ", ";
                out += elems[i];
            }
            out += "]";
            return types::Value(out, types::TypeID::Array);
        }
        if (func == "array_sum" || func == "array_avg" || func == "array_min" || func == "array_max") {
            if (args.empty()) return types::Value(0.0);
            auto elems = parse_array_elements(args[0].to_string());
            if (elems.empty()) return types::Value(0.0);
            double sum = 0.0;
            double min_v = 1e30, max_v = -1e30;
            for (const auto& e : elems) {
                double d = 0.0;
                try { d = std::stod(e); } catch (...) {}
                sum += d;
                if (d < min_v) min_v = d;
                if (d > max_v) max_v = d;
            }
            if (func == "array_sum") return types::Value(sum);
            if (func == "array_avg") return types::Value(sum / elems.size());
            if (func == "array_min") return types::Value(min_v < 1e29 ? min_v : 0.0);
            if (func == "array_max") return types::Value(max_v > -1e29 ? max_v : 0.0);
        }
        if (func == "array_to_string") {
            if (args.size() < 2) return types::Value("", types::TypeID::Text);
            auto elems = parse_array_elements(args[0].to_string());
            std::string delim = args[1].to_string();
            std::string out;
            for (size_t i = 0; i < elems.size(); ++i) {
                if (i > 0) out += delim;
                out += elems[i];
            }
            return types::Value(out, types::TypeID::Text);
        }
        if (func == "string_to_array") {
            if (args.size() < 2) return types::Value("[]", types::TypeID::Array);
            std::string str = args[0].to_string();
            std::string delim = args[1].to_string();
            std::string out = "[";
            size_t pos = 0;
            bool first = true;
            while (pos < str.size()) {
                size_t next = str.find(delim, pos);
                if (next == std::string::npos) next = str.size();
                if (!first) out += ", ";
                out += "\"" + str.substr(pos, next - pos) + "\"";
                first = false;
                pos = (next == str.size()) ? str.size() : (next + delim.size());
            }
            out += "]";
            return types::Value(out, types::TypeID::Array);
        }
        if (func == "array_to_json" || func == "json_to_array") {
            if (args.empty()) return types::Value("[]", types::TypeID::JSON);
            return types::Value(args[0].to_string(), types::TypeID::JSON);
        }

        // ---------------------------------------------------------------------
        // 4. DICT FUNCTIONS
        // ---------------------------------------------------------------------
        if (func == "dict_get") {
            if (args.size() < 2) return types::Value();
            auto entries = parse_dict_entries(args[0].to_string());
            std::string key = args[1].to_string();
            for (const auto& e : entries) {
                if (e.first == key) return parse_json_typed(e.second);
            }
            return types::Value();
        }
        if (func == "dict_has_key") {
            if (args.size() < 2) return types::Value(false);
            auto entries = parse_dict_entries(args[0].to_string());
            std::string key = args[1].to_string();
            for (const auto& e : entries) {
                if (e.first == key) return types::Value(true);
            }
            return types::Value(false);
        }
        if (func == "dict_keys") {
            return FunctionRegistry::evaluate("json_keys", args, db_state);
        }
        if (func == "dict_values") {
            return FunctionRegistry::evaluate("json_values", args, db_state);
        }
        if (func == "dict_size") {
            if (args.empty() || args[0].is_null()) return types::Value(static_cast<std::int64_t>(0));
            auto entries = parse_dict_entries(args[0].to_string());
            return types::Value(static_cast<std::int64_t>(entries.size()));
        }
        if (func == "dict_is_empty") {
            if (args.empty() || args[0].is_null()) return types::Value(true);
            auto entries = parse_dict_entries(args[0].to_string());
            return types::Value(entries.empty());
        }
        if (func == "dict_merge") {
            return FunctionRegistry::evaluate("json_merge", args, db_state);
        }
        if (func == "dict_remove") {
            if (args.size() < 2) return args.empty() ? types::Value() : args[0];
            auto entries = parse_dict_entries(args[0].to_string());
            std::string key = args[1].to_string();
            std::string out = "{";
            bool first = true;
            for (const auto& e : entries) {
                if (e.first != key) {
                    if (!first) out += ", ";
                    out += "\"" + e.first + "\": " + e.second;
                    first = false;
                }
            }
            out += "}";
            return types::Value(out, types::TypeID::DICT);
        }
        if (func == "dict_set") {
            if (args.size() < 3) return args.empty() ? types::Value() : args[0];
            auto entries = parse_dict_entries(args[0].to_string());
            std::string key = args[1].to_string();
            std::string val = args[2].to_string();
            bool found = false;
            for (auto& e : entries) {
                if (e.first == key) {
                    e.second = val;
                    found = true;
                    break;
                }
            }
            if (!found) entries.emplace_back(key, val);
            std::string out = "{";
            for (size_t i = 0; i < entries.size(); ++i) {
                if (i > 0) out += ", ";
                out += "\"" + entries[i].first + "\": " + entries[i].second;
            }
            out += "}";
            return types::Value(out, types::TypeID::DICT);
        }
        if (func == "dict_get_or") {
            if (args.size() < 2) return types::Value();
            auto entries = parse_dict_entries(args[0].to_string());
            std::string key = args[1].to_string();
            for (const auto& e : entries) {
                if (e.first == key) return parse_json_typed(e.second);
            }
            return (args.size() > 2) ? args[2] : types::Value();
        }
        if (func == "dict_to_json" || func == "json_to_dict") {
            if (args.empty()) return types::Value("{}", types::TypeID::DICT);
            return types::Value(args[0].to_string(), types::TypeID::DICT);
        }

        // ---------------------------------------------------------------------
        // 5. STRING FUNCTIONS
        // ---------------------------------------------------------------------
        if (func == "upper") {
            if (args.empty() || args[0].is_null()) return types::Value();
            return types::Value(to_upper(args[0].to_string()), types::TypeID::Text);
        }
        if (func == "lower") {
            if (args.empty() || args[0].is_null()) return types::Value();
            return types::Value(to_lower(args[0].to_string()), types::TypeID::Text);
        }
        if (func == "length" || func == "len" || func == "char_length") {
            if (args.empty() || args[0].is_null()) return types::Value(static_cast<std::int64_t>(0));
            return types::Value(static_cast<std::int64_t>(args[0].to_string().size()));
        }
        if (func == "octet_length" || func == "byte_length") {
            if (args.empty() || args[0].is_null()) return types::Value(static_cast<std::int64_t>(0));
            return types::Value(static_cast<std::int64_t>(args[0].to_string().size()));
        }
        if (func == "concat") {
            std::string res;
            for (const auto& a : args) {
                if (!a.is_null()) res += a.to_string();
            }
            return types::Value(res, types::TypeID::Text);
        }
        if (func == "concat_ws") {
            if (args.empty()) return types::Value("", types::TypeID::Text);
            std::string sep = args[0].to_string();
            std::string res;
            bool first = true;
            for (size_t i = 1; i < args.size(); ++i) {
                if (!args[i].is_null()) {
                    if (!first) res += sep;
                    res += args[i].to_string();
                    first = false;
                }
            }
            return types::Value(res, types::TypeID::Text);
        }
        if (func == "substr" || func == "substring") {
            if (args.empty() || args[0].is_null()) return types::Value("", types::TypeID::Text);
            std::string s = args[0].to_string();
            int64_t start = (args.size() > 1) ? std::stoll(args[1].to_string()) : 1;
            int64_t len = (args.size() > 2) ? std::stoll(args[2].to_string()) : static_cast<int64_t>(s.size());
            if (start < 1) start = 1;
            size_t idx0 = static_cast<size_t>(start - 1);
            if (idx0 >= s.size() || len <= 0) return types::Value("", types::TypeID::Text);
            return types::Value(s.substr(idx0, static_cast<size_t>(len)), types::TypeID::Text);
        }
        if (func == "trim") {
            if (args.empty() || args[0].is_null()) return types::Value("", types::TypeID::Text);
            return types::Value(trim(args[0].to_string()), types::TypeID::Text);
        }
        if (func == "ltrim") {
            if (args.empty() || args[0].is_null()) return types::Value("", types::TypeID::Text);
            std::string s = args[0].to_string();
            size_t p = 0;
            while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) p++;
            return types::Value(s.substr(p), types::TypeID::Text);
        }
        if (func == "rtrim") {
            if (args.empty() || args[0].is_null()) return types::Value("", types::TypeID::Text);
            std::string s = args[0].to_string();
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
            return types::Value(s, types::TypeID::Text);
        }
        if (func == "replace") {
            if (args.size() < 3) return args.empty() ? types::Value() : args[0];
            std::string s = args[0].to_string();
            std::string from = args[1].to_string();
            std::string to = args[2].to_string();
            if (from.empty()) return types::Value(s, types::TypeID::Text);
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos) {
                s.replace(pos, from.length(), to);
                pos += to.length();
            }
            return types::Value(s, types::TypeID::Text);
        }
        if (func == "position") {
            if (args.size() < 2) return types::Value(static_cast<std::int64_t>(0));
            std::string needle = args[0].to_string();
            std::string haystack = args[1].to_string();
            size_t pos = haystack.find(needle);
            return types::Value(static_cast<std::int64_t>(pos == std::string::npos ? 0 : pos + 1));
        }
        if (func == "split_part") {
            if (args.size() < 3) return types::Value("", types::TypeID::Text);
            std::string s = args[0].to_string();
            std::string delim = args[1].to_string();
            int64_t target_part = std::stoll(args[2].to_string());
            if (target_part < 1 || delim.empty()) return types::Value("", types::TypeID::Text);
            int64_t cur_part = 1;
            size_t p = 0;
            while (p < s.size()) {
                size_t next = s.find(delim, p);
                if (next == std::string::npos) next = s.size();
                if (cur_part == target_part) return types::Value(s.substr(p, next - p), types::TypeID::Text);
                cur_part++;
                p = (next == s.size()) ? s.size() : (next + delim.size());
            }
            return types::Value("", types::TypeID::Text);
        }
        if (func == "starts_with") {
            if (args.size() < 2) return types::Value(false);
            return types::Value(args[0].to_string().starts_with(args[1].to_string()));
        }
        if (func == "ends_with") {
            if (args.size() < 2) return types::Value(false);
            return types::Value(args[0].to_string().ends_with(args[1].to_string()));
        }
        if (func == "contains") {
            if (args.size() < 2) return types::Value(false);
            return types::Value(args[0].to_string().find(args[1].to_string()) != std::string::npos);
        }
        if (func == "repeat") {
            if (args.size() < 2) return types::Value("", types::TypeID::Text);
            std::string s = args[0].to_string();
            int64_t n = std::stoll(args[1].to_string());
            std::string res;
            for (int64_t i = 0; i < n; ++i) res += s;
            return types::Value(res, types::TypeID::Text);
        }
        if (func == "reverse") {
            if (args.empty()) return types::Value("", types::TypeID::Text);
            std::string s = args[0].to_string();
            std::reverse(s.begin(), s.end());
            return types::Value(s, types::TypeID::Text);
        }
        if (func == "lpad" || func == "rpad") {
            if (args.size() < 2) return args.empty() ? types::Value() : args[0];
            std::string s = args[0].to_string();
            size_t target_len = static_cast<size_t>(std::stoll(args[1].to_string()));
            std::string pad = (args.size() > 2) ? args[2].to_string() : " ";
            if (pad.empty()) pad = " ";
            if (s.size() >= target_len) return types::Value(s.substr(0, target_len), types::TypeID::Text);
            size_t diff = target_len - s.size();
            std::string padding;
            while (padding.size() < diff) padding += pad;
            padding = padding.substr(0, diff);
            return types::Value(func == "lpad" ? (padding + s) : (s + padding), types::TypeID::Text);
        }

        // ---------------------------------------------------------------------
        // 6. NUMERIC FUNCTIONS
        // ---------------------------------------------------------------------
        if (func == "round") {
            if (args.empty() || args[0].is_null()) return types::Value(0.0);
            double v = std::stod(args[0].to_string());
            int dec = (args.size() > 1) ? std::stoi(args[1].to_string()) : 0;
            double factor = std::pow(10.0, dec);
            return types::Value(std::round(v * factor) / factor);
        }
        if (func == "abs") {
            if (args.empty() || args[0].is_null()) return types::Value(0.0);
            return types::Value(std::abs(std::stod(args[0].to_string())));
        }
        if (func == "floor") {
            if (args.empty() || args[0].is_null()) return types::Value(static_cast<std::int64_t>(0));
            return types::Value(static_cast<std::int64_t>(std::floor(std::stod(args[0].to_string()))));
        }
        if (func == "ceil" || func == "ceiling") {
            if (args.empty() || args[0].is_null()) return types::Value(static_cast<std::int64_t>(0));
            return types::Value(static_cast<std::int64_t>(std::ceil(std::stod(args[0].to_string()))));
        }
        if (func == "trunc") {
            if (args.empty() || args[0].is_null()) return types::Value(0.0);
            double v = std::stod(args[0].to_string());
            int dec = (args.size() > 1) ? std::stoi(args[1].to_string()) : 0;
            double factor = std::pow(10.0, dec);
            return types::Value(std::trunc(v * factor) / factor);
        }
        if (func == "mod") {
            if (args.size() < 2) return types::Value(static_cast<std::int64_t>(0));
            int64_t a = std::stoll(args[0].to_string());
            int64_t b = std::stoll(args[1].to_string());
            return types::Value(static_cast<std::int64_t>(b != 0 ? (a % b) : 0));
        }
        if (func == "power") {
            if (args.size() < 2) return types::Value(0.0);
            return types::Value(std::pow(std::stod(args[0].to_string()), std::stod(args[1].to_string())));
        }
        if (func == "sqrt") {
            if (args.empty() || args[0].is_null()) return types::Value(0.0);
            double v = std::stod(args[0].to_string());
            return types::Value(v >= 0.0 ? std::sqrt(v) : 0.0);
        }
        if (func == "sign") {
            if (args.empty() || args[0].is_null()) return types::Value(static_cast<std::int64_t>(0));
            double v = std::stod(args[0].to_string());
            return types::Value(static_cast<std::int64_t>(v > 0.0 ? 1 : (v < 0.0 ? -1 : 0)));
        }
        if (func == "greatest") {
            if (args.empty()) return types::Value();
            double max_v = -1e30;
            bool found = false;
            bool all_int = true;
            for (const auto& a : args) {
                if (!a.is_null()) {
                    if (a.to_string().find('.') != std::string::npos) all_int = false;
                    double d = std::stod(a.to_string());
                    if (!found || d > max_v) { max_v = d; found = true; }
                }
            }
            if (!found) return types::Value();
            return all_int ? types::Value(static_cast<std::int64_t>(max_v)) : types::Value(max_v);
        }
        if (func == "least") {
            if (args.empty()) return types::Value();
            double min_v = 1e30;
            bool found = false;
            bool all_int = true;
            for (const auto& a : args) {
                if (!a.is_null()) {
                    if (a.to_string().find('.') != std::string::npos) all_int = false;
                    double d = std::stod(a.to_string());
                    if (!found || d < min_v) { min_v = d; found = true; }
                }
            }
            if (!found) return types::Value();
            return all_int ? types::Value(static_cast<std::int64_t>(min_v)) : types::Value(min_v);
        }

        // ---------------------------------------------------------------------
        // 7. NULL HANDLING & CASTING
        // ---------------------------------------------------------------------
        if (func == "coalesce" || func == "ifnull") {
            for (const auto& a : args) {
                if (!a.is_null()) return a;
            }
            return types::Value();
        }
        if (func == "nullif") {
            if (args.size() < 2) return args.empty() ? types::Value() : args[0];
            return (args[0].to_string() == args[1].to_string()) ? types::Value() : args[0];
        }
        if (func == "isnull") {
            if (args.empty()) return types::Value(true);
            return types::Value(args[0].is_null());
        }
        if (func == "typeof") {
            if (args.empty() || args[0].is_null()) return types::Value("null", types::TypeID::Text);
            switch (args[0].type()) {
                case types::TypeID::Boolean: return types::Value("boolean", types::TypeID::Text);
                case types::TypeID::SmallInt:
                case types::TypeID::Integer:
                case types::TypeID::BigInt: return types::Value("integer", types::TypeID::Text);
                case types::TypeID::Real:
                case types::TypeID::Double:
                case types::TypeID::Numeric: return types::Value("real", types::TypeID::Text);
                case types::TypeID::Char:
                case types::TypeID::VarChar:
                case types::TypeID::Text: return types::Value("text", types::TypeID::Text);
                case types::TypeID::Bytea: return types::Value("blob", types::TypeID::Text);
                case types::TypeID::JSON: return types::Value("json", types::TypeID::Text);
                case types::TypeID::JSONB: return types::Value("jsonb", types::TypeID::Text);
                case types::TypeID::Array: return types::Value("array", types::TypeID::Text);
                case types::TypeID::DICT: return types::Value("dict", types::TypeID::Text);
                case types::TypeID::UUID: return types::Value("uuid", types::TypeID::Text);
                case types::TypeID::Date: return types::Value("date", types::TypeID::Text);
                case types::TypeID::Time: return types::Value("time", types::TypeID::Text);
                case types::TypeID::Timestamp: return types::Value("timestamp", types::TypeID::Text);
                default: return types::Value("text", types::TypeID::Text);
            }
        }
        if (func == "cast" || func == "try_cast") {
            if (args.size() < 2) return args.empty() ? types::Value() : args[0];
            std::string target_type_str = to_upper(args[1].to_string());
            auto opt_type = types::TypeRegistry::lookup_type(target_type_str);
            if (!opt_type) {
                if (func == "try_cast") return types::Value();
                throw std::runtime_error("Unknown target type in CAST: " + target_type_str);
            }
            auto cast_res = types::TypeCast::cast(args[0], *opt_type);
            if (!cast_res) {
                if (func == "try_cast") return types::Value();
                throw std::runtime_error("Cannot cast value to " + target_type_str);
            }
            return *cast_res;
        }

        // ---------------------------------------------------------------------
        // 7.5 RANGE FUNCTIONS
        // ---------------------------------------------------------------------
        if (func == "range") {
            if (args.size() < 2) return types::Value("[0, 0)", types::TypeID::Text);
            return types::Value("[" + args[0].to_string() + ", " + args[1].to_string() + ")", types::TypeID::Text);
        }
        if (func == "range_lower") {
            if (args.empty() || args[0].is_null()) return types::Value();
            std::string s = args[0].to_string();
            size_t start = (s.front() == '[' || s.front() == '(') ? 1 : 0;
            size_t comma = s.find(',');
            if (comma != std::string::npos) {
                return parse_json_typed(trim(s.substr(start, comma - start)));
            }
            return types::Value();
        }
        if (func == "range_upper") {
            if (args.empty() || args[0].is_null()) return types::Value();
            std::string s = args[0].to_string();
            size_t comma = s.find(',');
            if (comma != std::string::npos) {
                size_t end = s.size();
                if (!s.empty() && (s.back() == ')' || s.back() == ']')) end--;
                return parse_json_typed(trim(s.substr(comma + 1, end - (comma + 1))));
            }
            return types::Value();
        }
        if (func == "range_contains") {
            if (args.size() < 2 || args[0].is_null()) return types::Value(false);
            std::string s = args[0].to_string();
            double pt = std::stod(args[1].to_string());
            size_t start = (s.front() == '[' || s.front() == '(') ? 1 : 0;
            size_t comma = s.find(',');
            if (comma != std::string::npos) {
                size_t end = s.size();
                if (!s.empty() && (s.back() == ')' || s.back() == ']')) end--;
                double low = std::stod(trim(s.substr(start, comma - start)));
                double high = std::stod(trim(s.substr(comma + 1, end - (comma + 1))));
                bool inc_low = (s.front() == '[');
                bool inc_high = (!s.empty() && s.back() == ']');
                bool ok_low = inc_low ? (pt >= low) : (pt > low);
                bool ok_high = inc_high ? (pt <= high) : (pt < high);
                return types::Value(ok_low && ok_high);
            }
            return types::Value(false);
        }

        // ---------------------------------------------------------------------
        // 8. TEMPORAL FUNCTIONS
        // ---------------------------------------------------------------------
        if (func == "current_date") {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::tm tm_buf;
#if defined(_WIN32)
            localtime_s(&tm_buf, &time_t);
#else
            localtime_r(&time_t, &tm_buf);
#endif
            std::stringstream ss;
            ss << std::put_time(&tm_buf, "%Y-%m-%d");
            return types::Value(ss.str(), types::TypeID::Date);
        }
        if (func == "current_time") {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::tm tm_buf;
#if defined(_WIN32)
            localtime_s(&tm_buf, &time_t);
#else
            localtime_r(&time_t, &tm_buf);
#endif
            std::stringstream ss;
            ss << std::put_time(&tm_buf, "%H:%M:%S");
            return types::Value(ss.str(), types::TypeID::Time);
        }
        if (func == "current_timestamp" || func == "now") {
            auto now_time = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now_time);
            std::tm tm_buf;
#if defined(_WIN32)
            localtime_s(&tm_buf, &time_t);
#else
            localtime_r(&time_t, &tm_buf);
#endif
            std::stringstream ss;
            ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
            return types::Value(ss.str(), types::TypeID::Timestamp);
        }
        if (func == "extract" || func == "date_part") {
            if (args.size() < 2) return types::Value(static_cast<std::int64_t>(0));
            std::string field = to_lower(args[0].to_string());
            std::string val_str = args[1].to_string();
            // Parse standard ISO timestamp YYYY-MM-DD[T ]HH:MM:SS
            int y = 2026, m = 1, d = 1, hr = 0, mn = 0, sc = 0;
            if (val_str.size() >= 10) {
                try {
                    y = std::stoi(val_str.substr(0, 4));
                    m = std::stoi(val_str.substr(5, 2));
                    d = std::stoi(val_str.substr(8, 2));
                    if (val_str.size() >= 19) {
                        hr = std::stoi(val_str.substr(11, 2));
                        mn = std::stoi(val_str.substr(14, 2));
                        sc = std::stoi(val_str.substr(17, 2));
                    }
                } catch (...) {}
            }
            if (field == "year") return types::Value(static_cast<std::int64_t>(y));
            if (field == "month") return types::Value(static_cast<std::int64_t>(m));
            if (field == "day") return types::Value(static_cast<std::int64_t>(d));
            if (field == "hour") return types::Value(static_cast<std::int64_t>(hr));
            if (field == "minute") return types::Value(static_cast<std::int64_t>(mn));
            if (field == "second") return types::Value(static_cast<std::int64_t>(sc));
            if (field == "epoch") return types::Value(static_cast<std::int64_t>(1792900000));
            return types::Value(static_cast<std::int64_t>(0));
        }

        // ---------------------------------------------------------------------
        // 9. BINARY & UUID FUNCTIONS
        // ---------------------------------------------------------------------
        if (func == "hex_encode") {
            if (args.empty()) return types::Value("", types::TypeID::Text);
            std::string s = args[0].to_string();
            std::stringstream ss;
            for (unsigned char c : s) ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
            return types::Value(ss.str(), types::TypeID::Text);
        }
        if (func == "hex_decode") {
            if (args.empty()) return types::Value("", types::TypeID::Bytea);
            std::string h = args[0].to_string();
            std::string res;
            for (size_t i = 0; i + 1 < h.size(); i += 2) {
                char byte = static_cast<char>(std::stoi(h.substr(i, 2), nullptr, 16));
                res.push_back(byte);
            }
            return types::Value(res, types::TypeID::Bytea);
        }
        if (func == "base64_encode") {
            if (args.empty()) return types::Value("", types::TypeID::Text);
            std::string s = args[0].to_string();
            std::vector<std::uint8_t> bytes(s.begin(), s.end());
            return types::Value(base64_encode(bytes), types::TypeID::Text);
        }
        if (func == "base64_decode") {
            if (args.empty()) return types::Value("", types::TypeID::Bytea);
            auto bytes = base64_decode(args[0].to_string());
            std::string res(bytes.begin(), bytes.end());
            return types::Value(res, types::TypeID::Bytea);
        }
        if (func == "uuid_generate") {
            std::random_device rd;
            std::mt19937_64 gen(rd());
            std::uint64_t p1 = gen();
            std::uint64_t p2 = gen();
            std::stringstream ss;
            ss << std::hex << std::setfill('0')
               << std::setw(8) << (p1 >> 32) << "-"
               << std::setw(4) << ((p1 >> 16) & 0xFFFF) << "-4"
               << std::setw(3) << (p1 & 0x0FFF) << "-a"
               << std::setw(3) << (p2 >> 60) << "-"
               << std::setw(12) << (p2 & 0xFFFFFFFFFFFFULL);
            return types::Value(ss.str(), types::TypeID::UUID);
        }
        if (func == "uuid_parse" || func == "uuid_to_string") {
            if (args.empty()) return types::Value();
            return types::Value(args[0].to_string(), types::TypeID::UUID);
        }

        // ---------------------------------------------------------------------
        // 10. BIT STRING FUNCTIONS
        // ---------------------------------------------------------------------
        if (func == "bit_length") {
            if (args.empty()) return types::Value(static_cast<std::int64_t>(0));
            return types::Value(static_cast<std::int64_t>(args[0].to_string().size()));
        }
        if (func == "bit_count") {
            if (args.empty()) return types::Value(static_cast<std::int64_t>(0));
            std::string s = args[0].to_string();
            int64_t cnt = 0;
            for (char c : s) if (c == '1') cnt++;
            return types::Value(cnt);
        }
        if (func == "bit_and") {
            if (args.size() < 2) return args.empty() ? types::Value() : args[0];
            std::string a = args[0].to_string();
            std::string b = args[1].to_string();
            std::string res;
            size_t n = std::min(a.size(), b.size());
            for (size_t i = 0; i < n; ++i) res.push_back((a[i] == '1' && b[i] == '1') ? '1' : '0');
            return types::Value(res, types::TypeID::Bit);
        }
        if (func == "bit_or") {
            if (args.size() < 2) return args.empty() ? types::Value() : args[0];
            std::string a = args[0].to_string();
            std::string b = args[1].to_string();
            std::string res;
            size_t n = std::min(a.size(), b.size());
            for (size_t i = 0; i < n; ++i) res.push_back((a[i] == '1' || b[i] == '1') ? '1' : '0');
            return types::Value(res, types::TypeID::Bit);
        }
        if (func == "bit_xor") {
            if (args.size() < 2) return args.empty() ? types::Value() : args[0];
            std::string a = args[0].to_string();
            std::string b = args[1].to_string();
            std::string res;
            size_t n = std::min(a.size(), b.size());
            for (size_t i = 0; i < n; ++i) res.push_back((a[i] != b[i]) ? '1' : '0');
            return types::Value(res, types::TypeID::Bit);
        }
        if (func == "bit_not") {
            if (args.empty()) return types::Value();
            std::string a = args[0].to_string();
            std::string res;
            for (char c : a) res.push_back(c == '1' ? '0' : '1');
            return types::Value(res, types::TypeID::Bit);
        }

        // ---------------------------------------------------------------------
        // 11. NETWORK FUNCTIONS
        // ---------------------------------------------------------------------
        if (func == "host" || func == "is_ipv4" || func == "is_ipv6") {
            if (args.empty()) return types::Value();
            std::string ip = args[0].to_string();
            if (func == "is_ipv4") return types::Value(ip.find('.') != std::string::npos);
            if (func == "is_ipv6") return types::Value(ip.find(':') != std::string::npos);
            size_t slash = ip.find('/');
            return types::Value(slash != std::string::npos ? ip.substr(0, slash) : ip, types::TypeID::Inet);
        }
        if (func == "netmask") {
            if (args.empty()) return types::Value("255.255.255.0", types::TypeID::Inet);
            std::string cidr = args[0].to_string();
            size_t slash = cidr.find('/');
            if (slash != std::string::npos) {
                int prefix = std::stoi(cidr.substr(slash + 1));
                if (prefix == 24) return types::Value("255.255.255.0", types::TypeID::Inet);
                if (prefix == 16) return types::Value("255.255.0.0", types::TypeID::Inet);
                if (prefix == 8) return types::Value("255.0.0.0", types::TypeID::Inet);
            }
            return types::Value("255.255.255.255", types::TypeID::Inet);
        }

        // Fallback: Return first argument or NULL
        return args.empty() ? types::Value() : args[0];
    }
};

} // namespace nemo::api::functions

#endif // NEMO_SRC_API_FUNCTION_REGISTRY_HPP
