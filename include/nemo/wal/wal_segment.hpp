#ifndef NEMO_WAL_WAL_SEGMENT_HPP
#define NEMO_WAL_WAL_SEGMENT_HPP

#include "nemo/wal/lsn.hpp"
#include <string>
#include <fstream>
#include <vector>
#include <cstdint>

namespace nemo::wal {

struct SegmentHeader {
    std::uint32_t magic; // e.g. 0x4B4C5741 "KLWA"
    std::uint32_t format_version;
    std::uint64_t segment_id;
    std::uint64_t start_lsn;
};

// Manages a single `.wal` segment file.
class WALSegment {
public:
    static constexpr std::uint32_t MAGIC = 0x4B4C5741; // "KLWA"
    static constexpr std::uint32_t FORMAT_VERSION = 1;
    static constexpr std::size_t HEADER_SIZE = 24;

    WALSegment(const std::string& directory, std::uint64_t segment_id);
    ~WALSegment();

    // Open for reading (existing segment)
    bool open_read();
    
    // Open for appending (new or existing segment)
    bool open_append(LSN start_lsn);
    
    void close();
    
    // Write data to the segment.
    bool append(const std::uint8_t* data, std::size_t size);
    
    // Ensure data is durable
    void flush();
    
    // Read from the segment
    bool read(std::uint64_t offset, std::uint8_t* buffer, std::size_t size);
    
    std::uint64_t get_size() const { return m_size; }
    std::uint64_t get_segment_id() const { return m_segment_id; }
    LSN get_start_lsn() const { return m_start_lsn; }
    std::string get_path() const { return m_path; }

private:
    std::string m_path;
    std::uint64_t m_segment_id;
    LSN m_start_lsn{LSN::invalid()};
    std::uint64_t m_size{0};
    
    std::fstream m_file;
    bool m_write_mode{false};
    
    std::string build_path(const std::string& directory, std::uint64_t segment_id);
};

} // namespace nemo::wal

#endif // NEMO_WAL_WAL_SEGMENT_HPP
