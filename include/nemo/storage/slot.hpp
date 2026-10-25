#ifndef NEMO_STORAGE_SLOT_HPP
#define NEMO_STORAGE_SLOT_HPP

#include <cstdint>

namespace nemo::storage {

// Represents an entry in the slotted page directory.
struct Slot {
    std::uint32_t offset; // Offset from the beginning of the page
    std::uint32_t length; // Length of the serialized record
    std::uint16_t flags;  // Metadata about the slot (e.g. deleted)

    static constexpr std::uint16_t FLAG_EMPTY = 0;
    static constexpr std::uint16_t FLAG_LIVE = 1 << 0;
    static constexpr std::uint16_t FLAG_DELETED = 1 << 1;

    bool is_empty() const noexcept { return flags == FLAG_EMPTY; }
    bool is_live() const noexcept { return (flags & FLAG_LIVE) != 0; }
    bool is_deleted() const noexcept { return (flags & FLAG_DELETED) != 0; }

    void set_live() noexcept { flags = FLAG_LIVE; }
    void set_deleted() noexcept { flags = FLAG_DELETED; }
    void set_empty() noexcept { flags = FLAG_EMPTY; offset = 0; length = 0; }
};

} // namespace nemo::storage

#endif // NEMO_STORAGE_SLOT_HPP
