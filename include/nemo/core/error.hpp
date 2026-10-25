#ifndef NEMO_CORE_ERROR_HPP
#define NEMO_CORE_ERROR_HPP

#include <stdexcept>
#include <string>

namespace nemo {

// Base class for all internal exceptions in Nemo.
// Expected database failures (e.g., duplicate key) should use Result<T> and Status.
// Exceptions are reserved for exceptional or unrecoverable internal conditions.
class NemoException : public std::runtime_error {
public:
    explicit NemoException(const std::string& message)
        : std::runtime_error(message) {}

    explicit NemoException(const char* message)
        : std::runtime_error(message) {}
};

// Specialized exceptions can be added here if needed in the future.

} // namespace nemo

#endif // NEMO_CORE_ERROR_HPP
