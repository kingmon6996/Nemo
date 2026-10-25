#include "nemo/wal/log_reader.hpp"
#include <filesystem>
#include <algorithm>
#include <iostream>

namespace nemo::wal {

LogReader::LogReader(const std::string& wal_directory) : m_wal_directory(wal_directory) {}

bool LogReader::initialize() {
    m_segment_ids.clear();
    
    if (!std::filesystem::exists(m_wal_directory)) {
        return false;
    }
    
    for (const auto& entry : std::filesystem::directory_iterator(m_wal_directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".wal") {
            try {
                std::uint64_t seg_id = std::stoull(entry.path().stem().string());
                m_segment_ids.push_back(seg_id);
            } catch (...) {
                // Ignore invalid filenames
            }
        }
    }
    
    std::sort(m_segment_ids.begin(), m_segment_ids.end());
    return !m_segment_ids.empty();
}

bool LogReader::open_segment(std::size_t index) {
    if (index >= m_segment_ids.size()) return false;
    
    m_current_segment = std::make_unique<WALSegment>(m_wal_directory, m_segment_ids[index]);
    if (!m_current_segment->open_read()) {
        m_current_segment.reset();
        return false;
    }
    
    m_current_segment_index = index;
    m_current_offset = WALSegment::HEADER_SIZE;
    return true;
}

bool LogReader::seek_to_first() {
    if (m_segment_ids.empty()) return false;
    return open_segment(0);
}

bool LogReader::seek_to_lsn(LSN lsn) {
    if (m_segment_ids.empty()) return false;
    
    // Find the segment containing this LSN
    // In a full implementation, we'd open segments and check m_start_lsn.
    // For simplicity, we just scan from the first segment until we hit the LSN.
    if (!seek_to_first()) return false;
    
    while (true) {
        auto record_opt = read_next();
        if (!record_opt) return false; // Hit EOF or corruption before finding LSN
        
        if (record_opt->get_lsn().value() >= lsn.value()) {
            // We found the record (or passed it). Rewind the offset so read_next() returns it again.
            // This is slightly inefficient but logically simple for module 11 demonstration.
            m_current_offset -= record_opt->get_size();
            return true;
        }
    }
}

std::optional<LogRecord> LogReader::read_next() {
    if (!m_current_segment) return std::nullopt;
    
    while (true) {
        std::uint64_t remaining = m_current_segment->get_size() - m_current_offset;
        
        if (remaining < LogRecord::HEADER_SIZE) {
            // End of this segment (or torn write). Move to next.
            if (open_segment(m_current_segment_index + 1)) {
                continue;
            } else {
                return std::nullopt; // EOF
            }
        }
        
        // Read the fixed header part to determine payload size
        std::vector<std::uint8_t> header_buf(LogRecord::HEADER_SIZE);
        if (!m_current_segment->read(m_current_offset, header_buf.data(), LogRecord::HEADER_SIZE)) {
            return std::nullopt; // I/O error
        }
        
        // The payload size is at offset 33 (8+8+8+8+1) in the header
        std::uint32_t payload_size = *reinterpret_cast<std::uint32_t*>(&header_buf[33]);
        
        if (remaining < LogRecord::HEADER_SIZE + payload_size) {
            // Torn write at the tail
            return std::nullopt; 
        }
        
        // Read full record
        std::uint32_t full_size = LogRecord::HEADER_SIZE + payload_size;
        std::vector<std::uint8_t> full_buf(full_size);
        if (!m_current_segment->read(m_current_offset, full_buf.data(), full_size)) {
            return std::nullopt;
        }
        
        LogRecord record;
        std::size_t parsed = record.deserialize(full_buf.data(), full_size);
        
        if (parsed == 0 || !record.verify_checksum()) {
            // Corruption or torn write
            std::cerr << "WAL Corruption/Torn Write detected at LSN " << record.get_lsn().value() << "\n";
            return std::nullopt;
        }
        
        m_current_offset += full_size;
        return record;
    }
}

} // namespace nemo::wal
