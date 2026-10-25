#ifndef NEMO_SQL_SQL_ERROR_HPP
#define NEMO_SQL_SQL_ERROR_HPP

#include "nemo/core/status.hpp"
#include "nemo/sql/source_location.hpp"
#include <string>

namespace nemo::sql {

class SQLError {
public:
    static Status make_error(Status base_status, const SourceLocation& loc, const std::string& message);
};

} // namespace nemo::sql

#endif // NEMO_SQL_SQL_ERROR_HPP
