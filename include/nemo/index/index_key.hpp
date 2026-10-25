#ifndef NEMO_INDEX_INDEX_KEY_HPP
#define NEMO_INDEX_INDEX_KEY_HPP

#include "nemo/types/value.hpp"
#include <span>
#include <vector>
#include <cstddef>

namespace nemo::index {

// Represents a logical search key which may be composed of one or more Values.
class IndexKey {
public:
    IndexKey() = default;
    
    // Single column key
    explicit IndexKey(types::Value val);
    
    // Multi-column (composite) key
    explicit IndexKey(std::vector<types::Value> values);

    std::size_t size() const noexcept { return m_values.size(); }
    
    const types::Value& at(std::size_t index) const { return m_values.at(index); }
    
    const std::vector<types::Value>& values() const noexcept { return m_values; }

    // Serialization for B+ Tree nodes
    std::vector<std::byte> serialize() const;
    static IndexKey deserialize(std::span<const std::byte> bytes, const std::vector<types::TypeID>& types);

private:
    std::vector<types::Value> m_values;
};

} // namespace nemo::index




#endif // NEMO_INDEX_INDEX_KEY_HPP
