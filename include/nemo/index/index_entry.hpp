#ifndef NEMO_INDEX_INDEX_ENTRY_HPP
#define NEMO_INDEX_INDEX_ENTRY_HPP

#include "nemo/index/index_key.hpp"
#include "nemo/storage/record_id.hpp"
#include "nemo/core/ids.hpp"

namespace nemo::index {

// An entry inside a Leaf Node: (Key -> RecordID)
struct LeafEntry {
    IndexKey key;
    storage::RecordID record_id;
};

// An entry inside an Internal Node: (Separator Key -> PageID)
struct InternalEntry {
    IndexKey key;
    PageID child_page_id;
};

} // namespace nemo::index

#endif // NEMO_INDEX_INDEX_ENTRY_HPP
