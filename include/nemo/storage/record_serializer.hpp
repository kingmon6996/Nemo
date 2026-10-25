#ifndef NEMO_STORAGE_RECORD_SERIALIZER_HPP
#define NEMO_STORAGE_RECORD_SERIALIZER_HPP

#include "nemo/storage/record.hpp"
#include "nemo/storage/tuple_layout.hpp"
#include <vector>
#include <cstddef>

namespace nemo::storage {

class RecordSerializer {
public:
    // Serialize a full record according to a specified tuple layout.
    static std::vector<std::byte> serialize(const Record& record, const TupleLayout& layout);
};

} // namespace nemo::storage

#endif // NEMO_STORAGE_RECORD_SERIALIZER_HPP
