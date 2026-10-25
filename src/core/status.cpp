#include "nemo/core/status.hpp"

namespace nemo {

std::string_view to_string(Status status) noexcept {
    switch (status) {
        case Status::OK: return "OK";
        case Status::InvalidArgument: return "InvalidArgument";
        case Status::NotFound: return "NotFound";
        case Status::AlreadyExists: return "AlreadyExists";
        case Status::InvalidState: return "InvalidState";
        case Status::IOError: return "IOError";
        case Status::Corruption: return "Corruption";
        case Status::Busy: return "Busy";
        case Status::Timeout: return "Timeout";
        case Status::TransactionAborted: return "TransactionAborted";
        case Status::Unsupported: return "Unsupported";
        case Status::InternalError: return "InternalError";
        default: return "Unknown";
    }
}

} // namespace nemo
