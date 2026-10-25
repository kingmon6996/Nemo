#ifndef NEMO_TYPES_DECIMAL_HPP
#define NEMO_TYPES_DECIMAL_HPP

#include <cstdint>
#include <string>

namespace nemo::types {

// Exact decimal representation.
// For V1, we use a 64-bit signed integer coefficient and a scale.
// Example: 12.34 -> coefficient = 1234, scale = 2
// Maximum representable value is around 9 x 10^18 with scale 0.
// Future versions can replace the coefficient with a BigInt implementation.
class Decimal {
public:
    Decimal() noexcept = default;
    
    // Construct from raw coefficient and scale
    Decimal(std::int64_t coefficient, std::uint8_t scale) noexcept
        : m_coefficient(coefficient), m_scale(scale) {}

    // Construct from integer
    explicit Decimal(std::int64_t value) noexcept
        : m_coefficient(value), m_scale(0) {}

    std::int64_t coefficient() const noexcept { return m_coefficient; }
    std::uint8_t scale() const noexcept { return m_scale; }

    // Comparisons
    bool operator==(const Decimal& other) const noexcept;
    bool operator!=(const Decimal& other) const noexcept { return !(*this == other); }
    bool operator<(const Decimal& other) const noexcept;
    bool operator>(const Decimal& other) const noexcept { return other < *this; }
    bool operator<=(const Decimal& other) const noexcept { return !(other < *this); }
    bool operator>=(const Decimal& other) const noexcept { return !(*this < other); }

    // Arithmetic
    Decimal operator+(const Decimal& other) const;
    Decimal operator-(const Decimal& other) const;
    Decimal operator*(const Decimal& other) const;
    Decimal operator/(const Decimal& other) const;

    // Formatting
    std::string to_string() const;

private:
    std::int64_t m_coefficient{0};
    std::uint8_t m_scale{0};
};

} // namespace nemo::types

#endif // NEMO_TYPES_DECIMAL_HPP
