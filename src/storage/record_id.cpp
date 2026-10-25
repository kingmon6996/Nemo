#include "nemo/storage/record_id.hpp"

namespace nemo::storage {

std::string RecordID::to_string() const {
    if (!is_valid()) {
        return "RecordID(INVALID)";
    }
    return "RecordID(page=" + std::to_string(m_page_id.value()) + ", slot=" + std::to_string(m_slot_id) + ")";
}

} // namespace nemo::storage
