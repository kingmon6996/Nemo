#ifndef NEMO_CORE_IDS_HPP
#define NEMO_CORE_IDS_HPP

#include <cstdint>
#include <functional>
#include <compare>
#include <limits>

namespace nemo {

// A template for creating strong ID types to prevent accidental mixing.
template <typename Tag, typename UnderlyingType = std::uint64_t>
class StrongID {
public:
    using value_type = UnderlyingType;

    constexpr StrongID() noexcept : m_value(invalid_value()) {}
    explicit constexpr StrongID(value_type value) noexcept : m_value(value) {}

    constexpr value_type value() const noexcept { return m_value; }
    constexpr bool is_valid() const noexcept { return m_value != invalid_value(); }

    auto operator<=>(const StrongID&) const = default;

    static constexpr value_type invalid_value() noexcept {
        return std::numeric_limits<value_type>::max();
    }

private:
    value_type m_value;
};

// Tag types
struct PageIDTag {};
struct RowIDTag {};
struct TableIDTag {};
struct IndexIDTag {};
struct TransactionIDTag {};
struct FrameIDTag {};

// Strong type definitions
using PageID = StrongID<PageIDTag>;
using RowID = StrongID<RowIDTag>;
using TableID = StrongID<TableIDTag>;
using IndexID = StrongID<IndexIDTag>;
using TransactionID = StrongID<TransactionIDTag>;
using FrameID = StrongID<FrameIDTag, std::uint32_t>; // FrameID is usually smaller

} // namespace nemo

namespace std {
template <typename Tag, typename UnderlyingType>
struct hash<nemo::StrongID<Tag, UnderlyingType>> {
    std::size_t operator()(const nemo::StrongID<Tag, UnderlyingType>& id) const noexcept {
        return std::hash<UnderlyingType>()(id.value());
    }
};
}

#endif // NEMO_CORE_IDS_HPP
