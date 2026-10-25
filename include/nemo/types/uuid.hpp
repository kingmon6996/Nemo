#ifndef NEMO_TYPES_UUID_HPP
#define NEMO_TYPES_UUID_HPP

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <optional>

namespace nemo::types {

class UUID {
public:
    UUID() noexcept;
    
    // Construct from raw 16 bytes
    explicit UUID(const std::array<std::uint8_t, 16>& bytes) noexcept;

    // Parse from a string (e.g., "550e8400-e29b-41d4-a716-446655440000")
    static std::optional<UUID> from_string(std::string_view str) noexcept;

    const std::array<std::uint8_t, 16>& data() const noexcept { return m_data; }

    std::string to_string() const;

    bool operator==(const UUID& other) const noexcept;
    bool operator!=(const UUID& other) const noexcept { return !(*this == other); }
    bool operator<(const UUID& other) const noexcept;
    bool operator>(const UUID& other) const noexcept { return other < *this; }
    bool operator<=(const UUID& other) const noexcept { return !(other < *this); }
    bool operator>=(const UUID& other) const noexcept { return !(*this < other); }

private:
    std::array<std::uint8_t, 16> m_data;
};

} // namespace nemo::types

namespace std {
    template <>
    struct hash<nemo::types::UUID> {
        std::size_t operator()(const nemo::types::UUID& uuid) const noexcept;
    };
}

#endif // NEMO_TYPES_UUID_HPP
