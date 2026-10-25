#ifndef NEMO_API_PREPARED_STATEMENT_HPP
#define NEMO_API_PREPARED_STATEMENT_HPP

#include "nemo/core/result.hpp"
#include "nemo/api/result.hpp"
#include "nemo/types/value.hpp"
#include <memory>
#include <string_view>
#include <vector>

namespace nemo::api {

class PreparedStatementImpl;

class PreparedStatement {
public:
    PreparedStatement(PreparedStatement&&) noexcept;
    PreparedStatement& operator=(PreparedStatement&&) noexcept;
    ~PreparedStatement();

    // Bind by 1-based index
    nemo::Result<void> bind(std::size_t index, const types::Value& value);
    
    // Bind by name (e.g. ":user_id")
    nemo::Result<void> bind(std::string_view name, const types::Value& value);

    nemo::Result<api::Result> execute();

    // Internal constructor
    explicit PreparedStatement(std::unique_ptr<PreparedStatementImpl> impl);

private:
    friend class Connection;
    
    std::unique_ptr<PreparedStatementImpl> m_impl;
};

} // namespace nemo::api

#endif // NEMO_API_PREPARED_STATEMENT_HPP
