#include "nemo/api/prepared_statement.hpp"
#include "api_internal.hpp"

namespace nemo::api {

class PreparedStatementImpl {
public:
    std::string sql;
    std::vector<types::Value> params;
};

PreparedStatement::PreparedStatement(std::unique_ptr<PreparedStatementImpl> impl) 
    : m_impl(std::move(impl)) {}
    
PreparedStatement::~PreparedStatement() = default;
PreparedStatement::PreparedStatement(PreparedStatement&&) noexcept = default;
PreparedStatement& PreparedStatement::operator=(PreparedStatement&&) noexcept = default;

nemo::Result<void> PreparedStatement::bind(std::size_t index, const types::Value& value) {
    if (!m_impl) return Status::InvalidState;
    if (index == 0) return Status::InvalidArgument; // 1-based
    if (index > m_impl->params.size()) {
        m_impl->params.resize(index);
    }
    m_impl->params[index - 1] = value;
    return nemo::Result<void>::success();
}

nemo::Result<void> PreparedStatement::bind(std::string_view /*name*/, const types::Value& /*value*/) {
    return Status::Unsupported;
}

nemo::Result<api::Result> PreparedStatement::execute() {
    return create_result(0, false);
}

PreparedStatement create_prepared_statement(std::string sql) {
    auto impl = std::make_unique<PreparedStatementImpl>();
    impl->sql = std::move(sql);
    return PreparedStatement(std::move(impl));
}

} // namespace nemo::api
