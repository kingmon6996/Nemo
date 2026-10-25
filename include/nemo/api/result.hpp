#ifndef NEMO_API_RESULT_HPP
#define NEMO_API_RESULT_HPP

#include "nemo/core/result.hpp"
#include "nemo/api/row.hpp"
#include <memory>
#include <vector>
#include <string>

namespace nemo::api {

struct ColumnMetadata {
    std::string name;
    int type; 
    bool nullable;
    std::string table;
    bool is_autoincrement{false};
    std::string sequence_name{""};
};

struct QueryStatistics {
    double planning_time_ms{0};
    double execution_time_ms{0};
    std::size_t rows_returned{0};
    std::size_t rows_scanned{0};
    std::size_t pages_read{0};
    std::size_t pages_written{0};
    std::size_t buffer_hits{0};
    std::size_t memory_used_bytes{0};
};

class ResultImpl;

class Result {
public:
    Result(Result&&) noexcept;
    Result& operator=(Result&&) noexcept;
    ~Result();

    // Iterator-like access
    bool has_rows() const noexcept;
    std::size_t affected_rows() const noexcept;
    
    // Fetch the next row
    std::unique_ptr<Row> next();
    
    std::vector<ColumnMetadata> columns() const;
    QueryStatistics statistics() const;

    // Internal constructor and impl accessor
    explicit Result(std::unique_ptr<ResultImpl> impl);
    ResultImpl* get_impl() noexcept { return m_impl.get(); }
    const ResultImpl* get_impl() const noexcept { return m_impl.get(); }

private:
    friend class Connection;
    friend class PreparedStatement;
    
    std::unique_ptr<ResultImpl> m_impl;
};

} // namespace nemo::api

#endif // NEMO_API_RESULT_HPP
