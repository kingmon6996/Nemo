#include "nemo/wal/wal_segment.hpp"
#include <sstream>
#include <iomanip>
#include <iostream>

// For OS-level fsync
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

namespace nemo::wal {

WALSegment::WALSegment(const std::string& directory, std::uint64_t segment_id)
    : m_segment_id(segment_id) {
    m_path = build_path(directory, segment_id);
}

WALSegment::~WALSegment() {
    close();
}

std::string WALSegment::build_path(const std::string& directory, std::uint64_t segment_id) {
    std::ostringstream oss;
    oss << directory << "/";
    if (directory.empty() || directory.back() != '/') {
        // oss << "/";
    }
    oss << std::setw(16) << std::setfill('0') << segment_id << ".wal";
    return oss.str();
}

bool WALSegment::open_read() {
    close();
    m_file.open(m_path, std::ios::in | std::ios::binary);
    if (!m_file.is_open()) return false;
    
    // Read header
    SegmentHeader header;
    m_file.read(reinterpret_cast<char*>(&header.magic), sizeof(header.magic));
    m_file.read(reinterpret_cast<char*>(&header.format_version), sizeof(header.format_version));
    m_file.read(reinterpret_cast<char*>(&header.segment_id), sizeof(header.segment_id));
    m_file.read(reinterpret_cast<char*>(&header.start_lsn), sizeof(header.start_lsn));
    
    if (m_file.gcount() != HEADER_SIZE) return false;
    if (header.magic != MAGIC || header.format_version != FORMAT_VERSION || header.segment_id != m_segment_id) {
        return false;
    }
    
    m_start_lsn = LSN(header.start_lsn);
    
    m_file.seekg(0, std::ios::end);
    m_size = static_cast<std::uint64_t>(m_file.tellg());
    m_file.seekg(HEADER_SIZE, std::ios::beg); // Reset to start of records
    
    m_write_mode = false;
    return true;
}

bool WALSegment::open_append(LSN start_lsn) {
    close();
    // Try to open existing
    m_file.open(m_path, std::ios::in | std::ios::out | std::ios::binary);
    
    if (m_file.is_open()) {
        // Validate existing
        SegmentHeader header;
        m_file.read(reinterpret_cast<char*>(&header.magic), sizeof(header.magic));
        if (header.magic == MAGIC) {
            m_file.seekg(0, std::ios::end);
            m_size = static_cast<std::uint64_t>(m_file.tellg());
            m_start_lsn = start_lsn; // Real implementation might read it
            m_write_mode = true;
            return true;
        } else {
            m_file.close(); // Invalid/empty, overwrite
        }
    }
    
    // Create new
    m_file.open(m_path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!m_file.is_open()) return false;
    
    SegmentHeader header;
    header.magic = MAGIC;
    header.format_version = FORMAT_VERSION;
    header.segment_id = m_segment_id;
    header.start_lsn = start_lsn.value();
    
    m_file.write(reinterpret_cast<const char*>(&header.magic), sizeof(header.magic));
    m_file.write(reinterpret_cast<const char*>(&header.format_version), sizeof(header.format_version));
    m_file.write(reinterpret_cast<const char*>(&header.segment_id), sizeof(header.segment_id));
    m_file.write(reinterpret_cast<const char*>(&header.start_lsn), sizeof(header.start_lsn));
    
    m_size = HEADER_SIZE;
    m_start_lsn = start_lsn;
    m_write_mode = true;
    
    return true;
}

void WALSegment::close() {
    if (m_file.is_open()) {
        if (m_write_mode) flush();
        m_file.close();
    }
}

bool WALSegment::append(const std::uint8_t* data, std::size_t size) {
    if (!m_write_mode || !m_file.is_open()) return false;
    
    m_file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!m_file.good()) return false;
    
    m_size += size;
    return true;
}

void WALSegment::flush() {
    if (!m_file.is_open() || !m_write_mode) return;
    
    m_file.flush();
    
    // OS-level fsync logic (Simplified)
#ifdef _WIN32
    // Windows: technically we'd need a HANDLE to use FlushFileBuffers. 
    // m_file.flush() combined with some std::fstream internals is often enough for testing.
    // We omit raw HANDLE extraction for standard C++ portability in this demo.
#else
    // POSIX
    // std::fstream doesn't trivially expose the fd. In a real engine, we'd use POSIX open/write/fsync natively.
    // For Module 11 abstraction, std::fstream flush acts as our barrier.
#endif
}

bool WALSegment::read(std::uint64_t offset, std::uint8_t* buffer, std::size_t size) {
    if (!m_file.is_open()) return false;
    
    m_file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!m_file.good()) return false;
    
    m_file.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(size));
    return m_file.gcount() == static_cast<std::streamsize>(size);
}

} // namespace nemo::wal
