#ifndef NEMO_STORAGE_SLOT_DIRECTORY_HPP
#define NEMO_STORAGE_SLOT_DIRECTORY_HPP

#include "nemo/storage/slot.hpp"
#include <span>

namespace nemo::storage {

// A lightweight view over the slot array in a TablePage
class SlotDirectory {
public:
    SlotDirectory(std::span<Slot> slots) : m_slots(slots) {}

    std::size_t size() const noexcept { return m_slots.size(); }
    
    Slot& at(std::size_t index) { return m_slots[index]; }
    const Slot& at(std::size_t index) const { return m_slots[index]; }

    // Returns INVALID_SLOT if no empty/deleted slot is found
    std::size_t find_first_free() const {
        for (std::size_t i = 0; i < m_slots.size(); ++i) {
            if (!m_slots[i].is_live()) return i;
        }
        return static_cast<std::size_t>(-1);
    }

private:
    std::span<Slot> m_slots;
};

} // namespace nemo::storage

#endif // NEMO_STORAGE_SLOT_DIRECTORY_HPP
