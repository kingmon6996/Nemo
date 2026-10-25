#ifndef NEMO_CORE_RESULT_HPP
#define NEMO_CORE_RESULT_HPP

#include "nemo/core/status.hpp"
#include <variant>
#include <type_traits>
#include <utility>
#include <stdexcept>

namespace nemo {

// Represents a successful result or a failure status.
template <typename T>
class Result {
public:
    // Value constructors
    Result(const T& value) : m_data(value) {}
    Result(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>) : m_data(std::move(value)) {}
    
    template <typename U, typename std::enable_if_t<std::is_constructible_v<T, U> && !std::is_same_v<std::decay_t<U>, Result<T>> && !std::is_same_v<std::decay_t<U>, Status>, int> = 0>
    Result(U&& value) : m_data(std::forward<U>(value)) {}

    // Status (error) constructor
    Result(Status status) noexcept : m_data(status) {}

    // Implicit boolean conversion to check for success
    explicit operator bool() const noexcept {
        return has_value();
    }

    bool has_value() const noexcept {
        return std::holds_alternative<T>(m_data);
    }

    // Accessors
    T& value() & {
        if (!has_value()) {
            throw std::runtime_error("Bad result access: no value");
        }
        return std::get<T>(m_data);
    }

    const T& value() const & {
        if (!has_value()) {
            throw std::runtime_error("Bad result access: no value");
        }
        return std::get<T>(m_data);
    }
    
    T&& value() && {
        if (!has_value()) {
            throw std::runtime_error("Bad result access: no value");
        }
        return std::get<T>(std::move(m_data));
    }

    Status error() const noexcept {
        if (std::holds_alternative<Status>(m_data)) {
            return std::get<Status>(m_data);
        }
        return Status::OK;
    }

private:
    std::variant<T, Status> m_data;
};

// Specialization for void
template <>
class Result<void> {
public:
    Result() noexcept : m_status(Status::OK), m_has_value(true) {}
    Result(Status status) noexcept : m_status(status), m_has_value(status == Status::OK) {}

    explicit operator bool() const noexcept {
        return m_has_value;
    }

    bool has_value() const noexcept {
        return m_has_value;
    }

    void value() const {
        if (!m_has_value) {
            throw std::runtime_error("Bad result access: no value");
        }
    }

    Status error() const noexcept {
        return m_status;
    }

    static Result<void> success() noexcept {
        return Result<void>();
    }

private:
    Status m_status{Status::OK};
    bool m_has_value{true};
};

} // namespace nemo

#endif // NEMO_CORE_RESULT_HPP
