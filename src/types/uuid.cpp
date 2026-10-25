#include "nemo/types/uuid.hpp"
#include <cstring>
#include <iomanip>
#include <sstream>

namespace nemo::types {

UUID::UUID() noexcept {
    m_data.fill(0);
}

UUID::UUID(const std::array<std::uint8_t, 16>& bytes) noexcept : m_data(bytes) {}

std::optional<UUID> UUID::from_string(std::string_view str) noexcept {
    if (str.length() != 36) return std::nullopt;

    std::array<std::uint8_t, 16> bytes;
    std::size_t byte_idx = 0;

    auto hex_to_val = [](char c) -> std::optional<std::uint8_t> {
        if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(c - 'A' + 10);
        return std::nullopt;
    };

    for (std::size_t i = 0; i < str.length(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (str[i] != '-') return std::nullopt;
            continue;
        }

        auto high = hex_to_val(str[i]);
        if (!high) return std::nullopt;
        
        ++i;
        if (i >= str.length()) return std::nullopt;
        
        auto low = hex_to_val(str[i]);
        if (!low) return std::nullopt;

        bytes[byte_idx++] = static_cast<std::uint8_t>((high.value() << 4) | low.value());
    }

    if (byte_idx != 16) return std::nullopt;
    return UUID(bytes);
}

std::string UUID::to_string() const {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    
    for (std::size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            ss << '-';
        }
        ss << std::setw(2) << static_cast<int>(m_data[i]);
    }
    
    return ss.str();
}

bool UUID::operator==(const UUID& other) const noexcept {
    return std::memcmp(m_data.data(), other.m_data.data(), 16) == 0;
}

bool UUID::operator<(const UUID& other) const noexcept {
    return std::memcmp(m_data.data(), other.m_data.data(), 16) < 0;
}

} // namespace nemo::types

namespace std {
    std::size_t hash<nemo::types::UUID>::operator()(const nemo::types::UUID& uuid) const noexcept {
        // Simple hash combining halves. Better hashing like MurmurHash can be added later.
        std::uint64_t high, low;
        std::memcpy(&high, uuid.data().data(), 8);
        std::memcpy(&low, uuid.data().data() + 8, 8);
        return hash<std::uint64_t>()(high) ^ (hash<std::uint64_t>()(low) << 1);
    }
}
