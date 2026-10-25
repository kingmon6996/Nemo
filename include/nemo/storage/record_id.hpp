#ifndef NEMO_STORAGE_RECORD_ID_HPP
#define NEMO_STORAGE_RECORD_ID_HPP

#include "nemo/core/ids.hpp"
#include <cstdint>
#include <string>

namespace nemo::storage {

// A stable physical record identifier: Page + Slot.
class RecordID {
public:
    static constexpr std::uint32_t INVALID_SLOT = static_cast<std::uint32_t>(-1);

    RecordID() noexcept = default;
    
    RecordID(PageID page_id, std::uint32_t slot_id) noexcept
        : m_page_id(page_id), m_slot_id(slot_id) {}

    PageID page_id() const noexcept { return m_page_id; }
    std::uint32_t slot_id() const noexcept { return m_slot_id; }

    bool is_valid() const noexcept {
        return m_page_id.is_valid() && m_slot_id != INVALID_SLOT;
    }

    bool operator==(const RecordID& other) const noexcept {
        return m_page_id == other.m_page_id && m_slot_id == other.m_slot_id;
    }

    bool operator!=(const RecordID& other) const noexcept {
        return !(*this == other);
    }

    std::string to_string() const;

private:
    PageID m_page_id{};
    std::uint32_t m_slot_id{INVALID_SLOT};
};

} // namespace nemo::storage

#endif // NEMO_STORAGE_RECORD_ID_HPP
