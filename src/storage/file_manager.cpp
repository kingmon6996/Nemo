#include "nemo/storage/file_manager.hpp"
#include "nemo/logging/logger.hpp"

namespace nemo::storage {

FileManager::FileManager() = default;

FileManager::~FileManager() {
    (void)close();
}

FileManager::FileManager(FileManager&& other) noexcept 
    : m_file(std::move(other.m_file)), m_is_open(other.m_is_open) {
    other.m_is_open = false;
}

FileManager& FileManager::operator=(FileManager&& other) noexcept {
    if (this != &other) {
        (void)close();
        m_file = std::move(other.m_file);
        m_is_open = other.m_is_open;
        other.m_is_open = false;
    }
    return *this;
}

Result<void> FileManager::create_file(const std::filesystem::path& path) {
    if (m_is_open) return Result<void>(Status::InvalidState);

    // Ensure it doesn't already exist
    if (std::filesystem::exists(path)) {
        return Result<void>(Status::AlreadyExists);
    }

    // Open for out (creates file) in binary mode
    m_file.open(path, std::ios::out | std::ios::binary);
    if (!m_file.is_open()) {
        return Result<void>(Status::IOError);
    }
    m_file.close();

    // Now open for read/write
    return open_file(path);
}

Result<void> FileManager::open_file(const std::filesystem::path& path) {
    if (m_is_open) return Result<void>(Status::InvalidState);

    m_file.open(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!m_file.is_open()) {
        return Result<void>(Status::IOError);
    }
    
    // Clear any potential error flags from previous operations
    m_file.clear(); 
    m_is_open = true;
    return Result<void>::success();
}

Result<void> FileManager::close() {
    if (m_is_open) {
        // Try flushing before close
        (void)flush();
        m_file.close();
        m_is_open = false;
        
        if (m_file.fail()) {
            return Result<void>(Status::IOError);
        }
    }
    return Result<void>::success();
}

bool FileManager::is_open() const noexcept {
    return m_is_open;
}

Result<void> FileManager::read(std::uint64_t offset, std::span<std::byte> buffer) {
    if (!m_is_open) return Result<void>(Status::InvalidState);

    m_file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (m_file.fail()) {
        m_file.clear();
        return Result<void>(Status::IOError);
    }

    m_file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    if (m_file.fail()) {
        m_file.clear();
        return Result<void>(Status::IOError);
    }
    
    return Result<void>::success();
}

Result<void> FileManager::write(std::uint64_t offset, std::span<const std::byte> buffer) {
    if (!m_is_open) return Result<void>(Status::InvalidState);

    m_file.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    if (m_file.fail()) {
        m_file.clear();
        return Result<void>(Status::IOError);
    }

    m_file.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    if (m_file.fail()) {
        m_file.clear();
        return Result<void>(Status::IOError);
    }

    return Result<void>::success();
}

Result<void> FileManager::flush() {
    if (!m_is_open) return Result<void>(Status::InvalidState);

    m_file.flush();
    if (m_file.fail()) {
        m_file.clear();
        return Result<void>(Status::IOError);
    }
    
    // Note: To be fully durable across crashes, we would need to call fsync() on POSIX
    // or FlushFileBuffers() on Windows on the underlying native file descriptor.
    // For Module 2, std::fstream::flush() pushes user-space buffers to the OS.
    return Result<void>::success();
}

Result<std::uint64_t> FileManager::file_size() const {
    if (!m_is_open) return Result<std::uint64_t>(Status::InvalidState);

    // Remember current position
    auto current_pos = m_file.tellg();
    
    m_file.seekg(0, std::ios::end);
    std::uint64_t size = static_cast<std::uint64_t>(m_file.tellg());
    
    // Restore
    m_file.seekg(current_pos, std::ios::beg);
    
    return size;
}

} // namespace nemo::storage
