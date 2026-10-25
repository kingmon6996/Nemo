#ifndef NEMO_CORE_VERSION_HPP
#define NEMO_CORE_VERSION_HPP

#include <string_view>

namespace nemo {

constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 1;
constexpr int VERSION_PATCH = 0;

// Storage format version (independent from library version)
constexpr int STORAGE_FORMAT_VERSION = 1;

// Returns the human-readable library version string (e.g., "0.1.0")
std::string_view version() noexcept;

} // namespace nemo

#endif // NEMO_CORE_VERSION_HPP
