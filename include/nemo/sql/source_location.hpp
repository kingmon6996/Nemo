#ifndef NEMO_SQL_SOURCE_LOCATION_HPP
#define NEMO_SQL_SOURCE_LOCATION_HPP

#include <cstddef>
#include <string>

namespace nemo::sql {

struct SourceLocation {
    std::size_t offset{0};
    std::size_t line{1};
    std::size_t column{1};

    std::string to_string() const;
};

} // namespace nemo::sql

#endif // NEMO_SQL_SOURCE_LOCATION_HPP
