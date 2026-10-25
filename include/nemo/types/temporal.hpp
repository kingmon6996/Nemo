#ifndef NEMO_TYPES_TEMPORAL_HPP
#define NEMO_TYPES_TEMPORAL_HPP

#include <cstdint>
#include <string>

namespace nemo::types {

// Date represents a calendar date. 
// Internally stored as a 32-bit integer: days since PostgreSQL epoch (2000-01-01).
class Date {
public:
    Date() noexcept = default;
    explicit Date(std::int32_t days_since_epoch) noexcept : m_days(days_since_epoch) {}

    std::int32_t days() const noexcept { return m_days; }

    bool operator==(const Date& other) const noexcept { return m_days == other.m_days; }
    bool operator!=(const Date& other) const noexcept { return !(*this == other); }
    bool operator<(const Date& other) const noexcept { return m_days < other.m_days; }
    bool operator>(const Date& other) const noexcept { return other < *this; }
    bool operator<=(const Date& other) const noexcept { return !(other < *this); }
    bool operator>=(const Date& other) const noexcept { return !(*this < other); }

    std::string to_string() const;

private:
    std::int32_t m_days{0};
};

// Time represents time of day.
// Internally stored as a 64-bit integer: microseconds since midnight.
class Time {
public:
    Time() noexcept = default;
    explicit Time(std::int64_t micros_since_midnight) noexcept : m_micros(micros_since_midnight) {}

    std::int64_t microseconds() const noexcept { return m_micros; }

    bool operator==(const Time& other) const noexcept { return m_micros == other.m_micros; }
    bool operator!=(const Time& other) const noexcept { return !(*this == other); }
    bool operator<(const Time& other) const noexcept { return m_micros < other.m_micros; }
    bool operator>(const Time& other) const noexcept { return other < *this; }
    bool operator<=(const Time& other) const noexcept { return !(other < *this); }
    bool operator>=(const Time& other) const noexcept { return !(*this < other); }

    std::string to_string() const;

private:
    std::int64_t m_micros{0};
};

// Timestamp represents an absolute point in time.
// Internally stored as a 64-bit integer: microseconds since PostgreSQL epoch (2000-01-01).
class Timestamp {
public:
    Timestamp() noexcept = default;
    explicit Timestamp(std::int64_t micros_since_epoch) noexcept : m_micros(micros_since_epoch) {}

    std::int64_t microseconds() const noexcept { return m_micros; }

    bool operator==(const Timestamp& other) const noexcept { return m_micros == other.m_micros; }
    bool operator!=(const Timestamp& other) const noexcept { return !(*this == other); }
    bool operator<(const Timestamp& other) const noexcept { return m_micros < other.m_micros; }
    bool operator>(const Timestamp& other) const noexcept { return other < *this; }
    bool operator<=(const Timestamp& other) const noexcept { return !(other < *this); }
    bool operator>=(const Timestamp& other) const noexcept { return !(*this < other); }

    std::string to_string() const;

private:
    std::int64_t m_micros{0};
};

// Interval represents a span of time.
// Stored as months, days, and microseconds to correctly handle leap years and daylight saving transitions.
class Interval {
public:
    Interval() noexcept = default;
    Interval(std::int32_t months, std::int32_t days, std::int64_t micros) noexcept
        : m_months(months), m_days(days), m_micros(micros) {}

    std::int32_t months() const noexcept { return m_months; }
    std::int32_t days() const noexcept { return m_days; }
    std::int64_t microseconds() const noexcept { return m_micros; }

    bool operator==(const Interval& other) const noexcept {
        return m_months == other.m_months && m_days == other.m_days && m_micros == other.m_micros;
    }
    bool operator!=(const Interval& other) const noexcept { return !(*this == other); }
    
    bool operator<(const Interval& other) const noexcept {
        if (m_months != other.m_months) return m_months < other.m_months;
        if (m_days != other.m_days) return m_days < other.m_days;
        return m_micros < other.m_micros;
    }
    
    std::string to_string() const;

private:
    std::int32_t m_months{0};
    std::int32_t m_days{0};
    std::int64_t m_micros{0};
};

} // namespace nemo::types

#endif // NEMO_TYPES_TEMPORAL_HPP
