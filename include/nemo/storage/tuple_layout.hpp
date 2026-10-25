#ifndef NEMO_STORAGE_TUPLE_LAYOUT_HPP
#define NEMO_STORAGE_TUPLE_LAYOUT_HPP

#include "nemo/types/type_id.hpp"
#include <vector>
#include <cstdint>

namespace nemo::storage {

// Describes the structure of a record for a specific table.
class TupleLayout {
public:
    struct Column {
        types::TypeID type;
        std::uint32_t offset; // For fixed length fields. 0 if variable.
        std::uint32_t fixed_length; // 0 if variable length.
    };

    TupleLayout() = default;

    // Add a column to the layout
    void add_column(types::TypeID type);

    std::size_t column_count() const noexcept { return m_columns.size(); }
    const Column& column(std::size_t index) const { return m_columns.at(index); }
    
    // Total size of all fixed length fields
    std::uint32_t fixed_part_size() const noexcept { return m_fixed_size; }
    
    // Number of variable length fields
    std::uint32_t variable_count() const noexcept { return m_variable_count; }

private:
    std::vector<Column> m_columns;
    std::uint32_t m_fixed_size{0};
    std::uint32_t m_variable_count{0};
};

} // namespace nemo::storage

#endif // NEMO_STORAGE_TUPLE_LAYOUT_HPP
