#ifndef NEMO_STORAGE_RECORD_HPP
#define NEMO_STORAGE_RECORD_HPP

#include "nemo/types/value.hpp"
#include <vector>

namespace nemo::storage {

// Represents an in-memory row. Essentially a container of Values.
class Record {
public:
    Record() = default;
    explicit Record(std::vector<types::Value> fields) : m_fields(std::move(fields)) {}

    std::size_t field_count() const noexcept { return m_fields.size(); }
    
    const types::Value& field(std::size_t index) const {
        return m_fields.at(index);
    }
    
    types::Value& field(std::size_t index) {
        return m_fields.at(index);
    }

    const std::vector<types::Value>& fields() const noexcept { return m_fields; }
    std::vector<types::Value>& fields() noexcept { return m_fields; }

    bool operator==(const Record& other) const {
        if (m_fields.size() != other.m_fields.size()) return false;
        for (std::size_t i = 0; i < m_fields.size(); ++i) {
            if (m_fields[i] != other.m_fields[i]) return false;
        }
        return true;
    }

private:
    std::vector<types::Value> m_fields;
};

} // namespace nemo::storage

#endif // NEMO_STORAGE_RECORD_HPP
