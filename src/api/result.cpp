#include "nemo/api/result.hpp"
#include "nemo/api/row.hpp"
#include "api_internal.hpp"

namespace nemo::api {

Result::Result(std::unique_ptr<ResultImpl> impl) : m_impl(std::move(impl)) {}
Result::~Result() = default;
Result::Result(Result&&) noexcept = default;
Result& Result::operator=(Result&&) noexcept = default;

bool Result::has_rows() const noexcept {
    return m_impl ? m_impl->has_rows : false;
}

std::size_t Result::affected_rows() const noexcept {
    return m_impl ? m_impl->affected_rows : 0;
}

std::unique_ptr<Row> Result::next() {
    return m_impl ? m_impl->create_next_row() : nullptr;
}

std::vector<ColumnMetadata> Result::columns() const {
    return m_impl ? m_impl->columns : std::vector<ColumnMetadata>{};
}

QueryStatistics Result::statistics() const {
    return m_impl ? m_impl->stats : QueryStatistics{};
}

Result create_result(
    std::size_t affected,
    bool has_rows,
    std::vector<ColumnMetadata> cols,
    std::vector<std::vector<types::Value>> rows,
    std::size_t total_rows,
    bool is_exact
) {
    auto impl = std::make_unique<ResultImpl>();
    impl->affected_rows = affected;
    impl->has_rows = has_rows;
    impl->is_exact = is_exact;
    impl->columns = std::move(cols);
    impl->total_rows = (total_rows > 0) ? total_rows : rows.size();
    impl->rows = std::move(rows);
    return Result(std::move(impl));
}

} // namespace nemo::api
