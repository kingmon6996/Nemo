#ifndef NEMO_STORAGE_RECORD_DESERIALIZER_HPP
#define NEMO_STORAGE_RECORD_DESERIALIZER_HPP

#include "nemo/storage/record.hpp"
#include "nemo/storage/record_view.hpp"
#include <span>
#include <cstddef>

namespace nemo::storage {

class RecordDeserializer {
public:
    // Deserialize a full record (materializes all Values)
    static Record deserialize(std::span<const std::byte> bytes, const TupleLayout& layout);
};

} // namespace nemo::storage

#endif // NEMO_STORAGE_RECORD_DESERIALIZER_HPP
