#ifndef NEMO_TYPES_DICT_HPP
#define NEMO_TYPES_DICT_HPP

#include "nemo/types/type_id.hpp"
#include <string>
#include <vector>

namespace nemo::types {

class Value; // Forward declare

// A dictionary that maps string keys to Value.
class Dict {
public:
    Dict();
    ~Dict();

    // Copy and move semantics
    Dict(const Dict& other);
    Dict& operator=(const Dict& other);
    Dict(Dict&& other) noexcept;
    Dict& operator=(Dict&& other) noexcept;

    std::size_t size() const noexcept;
    bool empty() const noexcept;

    bool contains(const std::string& key) const;
    const Value* get(const std::string& key) const; // Returns nullptr if not found
    void set(const std::string& key, const Value& value);
    bool remove(const std::string& key); // Returns true if removed
    void clear();

    std::vector<std::string> keys() const;
    // Values and items can be added if needed, but returning std::vector<Value> requires complete type.
    // For now, we provide stringification and basic operations.

    bool operator==(const Dict& other) const;

    std::string to_string() const;

private:
    struct Impl;
    Impl* m_impl;
};

} // namespace nemo::types

#endif // NEMO_TYPES_DICT_HPP
