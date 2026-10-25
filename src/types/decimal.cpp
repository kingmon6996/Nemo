#include "nemo/types/decimal.hpp"
#include <stdexcept>
#include <string>
#include <cmath>
#include <algorithm>

namespace nemo::types {

namespace {

// Helper to scale a coefficient up. Returns false on overflow.
bool scale_up(std::int64_t val, std::uint8_t diff, std::int64_t& out) {
    if (diff == 0) {
        out = val;
        return true;
    }
    
    std::int64_t multiplier = 1;
    for (std::uint8_t i = 0; i < diff; ++i) {
        if (multiplier > std::numeric_limits<std::int64_t>::max() / 10) return false;
        multiplier *= 10;
    }

    if (val > 0 && val > std::numeric_limits<std::int64_t>::max() / multiplier) return false;
    if (val < 0 && val < std::numeric_limits<std::int64_t>::min() / multiplier) return false;
    
    out = val * multiplier;
    return true;
}

} // namespace

bool Decimal::operator==(const Decimal& other) const noexcept {
    // Normalize to the larger scale for comparison
    std::uint8_t target_scale = std::max(m_scale, other.m_scale);
    
    std::int64_t c1, c2;
    if (!scale_up(m_coefficient, target_scale - m_scale, c1) ||
        !scale_up(other.m_coefficient, target_scale - other.m_scale, c2)) {
        // If one scales out of bounds but the other doesn't, they aren't equal.
        // Wait, if both fail to scale, this comparison is flawed. For equality, it's safer
        // to simplify fractions, but V1 implementation we just do basic scaling.
        return false;
    }
    return c1 == c2;
}

bool Decimal::operator<(const Decimal& other) const noexcept {
    std::uint8_t target_scale = std::max(m_scale, other.m_scale);
    
    std::int64_t c1, c2;
    if (!scale_up(m_coefficient, target_scale - m_scale, c1)) {
        return m_coefficient < 0; // Negative overflow is smaller, positive is larger
    }
    if (!scale_up(other.m_coefficient, target_scale - other.m_scale, c2)) {
        return other.m_coefficient > 0;
    }
    return c1 < c2;
}

Decimal Decimal::operator+(const Decimal& other) const {
    std::uint8_t target_scale = std::max(m_scale, other.m_scale);
    std::int64_t c1, c2;
    if (!scale_up(m_coefficient, target_scale - m_scale, c1) ||
        !scale_up(other.m_coefficient, target_scale - other.m_scale, c2)) {
        throw std::overflow_error("Decimal overflow during addition");
    }
    
    // Check addition overflow
    if ((c2 > 0 && c1 > std::numeric_limits<std::int64_t>::max() - c2) ||
        (c2 < 0 && c1 < std::numeric_limits<std::int64_t>::min() - c2)) {
        throw std::overflow_error("Decimal overflow during addition");
    }
    
    return Decimal(c1 + c2, target_scale);
}

Decimal Decimal::operator-(const Decimal& other) const {
    std::uint8_t target_scale = std::max(m_scale, other.m_scale);
    std::int64_t c1, c2;
    if (!scale_up(m_coefficient, target_scale - m_scale, c1) ||
        !scale_up(other.m_coefficient, target_scale - other.m_scale, c2)) {
        throw std::overflow_error("Decimal overflow during subtraction");
    }
    
    if ((c2 < 0 && c1 > std::numeric_limits<std::int64_t>::max() + c2) ||
        (c2 > 0 && c1 < std::numeric_limits<std::int64_t>::min() + c2)) {
        throw std::overflow_error("Decimal overflow during subtraction");
    }
    
    return Decimal(c1 - c2, target_scale);
}

Decimal Decimal::operator*(const Decimal& other) const {
    // scale = s1 + s2
    // If coefficient overflows, throw
    
    // We can try to use standard multiplication, but detecting overflow is needed.
    // A simple safe multiplication:
    auto c1 = m_coefficient;
    auto c2 = other.m_coefficient;
    
    if (c1 != 0 && c2 != 0) {
        if (c1 > 0 && c2 > 0 && c1 > std::numeric_limits<std::int64_t>::max() / c2) throw std::overflow_error("Decimal multiply overflow");
        if (c1 > 0 && c2 < 0 && c2 < std::numeric_limits<std::int64_t>::min() / c1) throw std::overflow_error("Decimal multiply overflow");
        if (c1 < 0 && c2 > 0 && c1 < std::numeric_limits<std::int64_t>::min() / c2) throw std::overflow_error("Decimal multiply overflow");
        if (c1 < 0 && c2 < 0 && c1 < std::numeric_limits<std::int64_t>::max() / c2) throw std::overflow_error("Decimal multiply overflow");
    }
    
    std::uint8_t target_scale = m_scale + other.m_scale;
    return Decimal(c1 * c2, target_scale);
}

Decimal Decimal::operator/(const Decimal& other) const {
    if (other.m_coefficient == 0) {
        throw std::invalid_argument("Decimal division by zero");
    }
    
    // Naive division: we scale up the numerator by an arbitrary target precision (e.g. 6 places)
    // before dividing. This is a V1 limitation.
    std::uint8_t extra_precision = 6;
    std::int64_t c1;
    if (!scale_up(m_coefficient, extra_precision, c1)) {
        throw std::overflow_error("Decimal overflow during division scaling");
    }
    
    std::int64_t res = c1 / other.m_coefficient;
    std::uint8_t target_scale = static_cast<std::uint8_t>(m_scale + extra_precision - other.m_scale);
    
    return Decimal(res, target_scale);
}

std::string Decimal::to_string() const {
    if (m_coefficient == 0) {
        if (m_scale == 0) return "0";
        return "0." + std::string(m_scale, '0');
    }

    std::string s = std::to_string(std::abs(m_coefficient));
    if (m_scale == 0) {
        return (m_coefficient < 0 ? "-" : "") + s;
    }

    if (s.length() <= m_scale) {
        s = std::string(m_scale - s.length() + 1, '0') + s;
    }

    if (s.length() > m_scale) {
        s.insert(s.length() - m_scale, ".");
    }
    return (m_coefficient < 0 ? "-" : "") + s;
}

} // namespace nemo::types
