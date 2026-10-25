#ifndef NEMO_STORAGE_FILE_MANAGER_HPP
#define NEMO_STORAGE_FILE_MANAGER_HPP

#include "nemo/core/result.hpp"
#include <filesystem>
#include <fstream>
#include <span>

namespace nemo::storage {

// Low-level OS/file abstraction for reading and writing exact byte ranges.
class FileManager {
public:
    FileManager();
    ~FileManager();

    // Disable copy, allow move
    FileManager(const FileManager&) = delete;
    FileManager& operator=(const FileManager&) = delete;
    FileManager(FileManager&&) noexcept;
    FileManager& operator=(FileManager&&) noexcept;

    Result<void> create_file(const std::filesystem::path& path);
    Result<void> open_file(const std::filesystem::path& path);
    Result<void> close();
    bool is_open() const noexcept;

    Result<void> read(std::uint64_t offset, std::span<std::byte> buffer);
    Result<void> write(std::uint64_t offset, std::span<const std::byte> buffer);
    Result<void> flush();

    Result<std::uint64_t> file_size() const;

private:
    mutable std::fstream m_file;
    bool m_is_open{false};
};

} // namespace nemo::storage

#endif // NEMO_STORAGE_FILE_MANAGER_HPP
