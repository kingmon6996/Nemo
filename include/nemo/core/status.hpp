#ifndef NEMO_CORE_STATUS_HPP
#define NEMO_CORE_STATUS_HPP

#include <string_view>

namespace nemo {

// Expected database statuses
enum class Status {
    OK,
    InvalidArgument,
    NotFound,
    AlreadyExists,
    InvalidState,
    IOError,
    Corruption,
    Busy,
    Timeout,
    TransactionAborted,
    Unsupported,
    InternalError
};

// Convert a Status to a human-readable string
std::string_view to_string(Status status) noexcept;

} // namespace nemo

#endif // NEMO_CORE_STATUS_HPP
