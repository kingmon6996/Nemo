#ifndef NEMO_WAL_LOG_READER_HPP
#define NEMO_WAL_LOG_READER_HPP

#include "nemo/wal/log_record.hpp"
#include "nemo/wal/wal_segment.hpp"
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace nemo::wal {

// Sequentially reads LogRecords from WAL segments for Recovery
class LogReader {
public:
    explicit LogReader(const std::string& wal_directory);
    
    // Scans the directory for all WAL segments, sorting them by segment ID.
    bool initialize();

    // Start reading from a specific LSN (e.g. from a Checkpoint)
    bool seek_to_lsn(LSN lsn);
    
    // Start reading from the very beginning of the available WAL
    bool seek_to_first();

    // Read the next complete, valid record. Returns nullopt if EOF or corruption/incomplete record is hit.
    std::optional<LogRecord> read_next();

private:
    std::string m_wal_directory;
    std::vector<std::uint64_t> m_segment_ids;
    
    std::unique_ptr<WALSegment> m_current_segment;
    std::size_t m_current_segment_index{0};
    
    std::uint64_t m_current_offset{0};
    
    bool open_segment(std::size_t index);
};

} // namespace nemo::wal

#endif // NEMO_WAL_LOG_READER_HPP
