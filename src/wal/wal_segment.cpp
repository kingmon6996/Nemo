#include "nemo/wal/wal_segment.hpp"
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

// For OS-level fsync
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace nemo::wal {

WALSegment::WALSegment(const std::string& directory, std::uint64_t segment_id)
    : m_segment_id(segment_id) {
    m_path = build_path(directory, segment_id);
}

WALSegment::~WALSegment() {
    try {
        close();
    } catch (...) {
    }
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
    m_file.clear();
    m_file.open(m_path, std::ios::in | std::ios::binary);
    if (!m_file.is_open()) return false;
    
    // Read header
    SegmentHeader header;
    m_file.read(reinterpret_cast<char*>(&header.magic), sizeof(header.magic));
    m_file.read(reinterpret_cast<char*>(&header.format_version), sizeof(header.format_version));
    m_file.read(reinterpret_cast<char*>(&header.segment_id), sizeof(header.segment_id));
    m_file.read(reinterpret_cast<char*>(&header.start_lsn), sizeof(header.start_lsn));
    
    if (!m_file) return false;
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
    m_file.clear();
    if (std::filesystem::exists(m_path)) {
        m_file.open(m_path, std::ios::in | std::ios::out | std::ios::binary);
        if (!m_file.is_open()) return false;

        SegmentHeader header;
        m_file.read(reinterpret_cast<char*>(&header.magic), sizeof(header.magic));
        m_file.read(reinterpret_cast<char*>(&header.format_version), sizeof(header.format_version));
        m_file.read(reinterpret_cast<char*>(&header.segment_id), sizeof(header.segment_id));
        m_file.read(reinterpret_cast<char*>(&header.start_lsn), sizeof(header.start_lsn));
        if (!m_file || header.magic != MAGIC ||
            header.format_version != FORMAT_VERSION ||
            header.segment_id != m_segment_id) {
            m_file.close();
            return false;
        }

        m_file.seekg(0, std::ios::end);
        const auto end = m_file.tellg();
        if (end < static_cast<std::streamoff>(HEADER_SIZE)) {
            m_file.close();
            return false;
        }
        m_size = static_cast<std::uint64_t>(end);
        m_start_lsn = LSN(header.start_lsn);
        m_file.clear();
        m_file.seekp(0, std::ios::end);
        if (!m_file) {
            m_file.close();
            return false;
        }
        m_write_mode = true;
        return true;
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
    return static_cast<bool>(m_file);
}

void WALSegment::close() {
    if (m_file.is_open()) {
        if (m_write_mode) flush();
        m_file.close();
        if (m_file.fail()) {
            m_file.clear();
            throw std::runtime_error("Failed to close WAL segment.");
        }
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
    if (!m_file) {
        throw std::runtime_error("Failed to flush WAL segment stream.");
    }

#ifdef _WIN32
    HANDLE file = CreateFileW(
        std::filesystem::path(m_path).c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Failed to open WAL segment for durable flush.");
    }
    const BOOL flushed = FlushFileBuffers(file);
    const DWORD flush_error = flushed ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!flushed) {
        throw std::runtime_error(
            "Failed to durably flush WAL segment (Windows error " +
            std::to_string(flush_error) + ").");
    }
#else
    const int file = ::open(m_path.c_str(), O_RDWR);
    if (file < 0) {
        throw std::runtime_error("Failed to open WAL segment for durable flush.");
    }
    const int flush_result = ::fsync(file);
    const int flush_error = errno;
    ::close(file);
    if (flush_result != 0) {
        throw std::runtime_error(
            "Failed to durably flush WAL segment (errno " +
            std::to_string(flush_error) + ").");
    }
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
