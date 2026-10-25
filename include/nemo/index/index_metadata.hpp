#ifndef NEMO_INDEX_INDEX_METADATA_HPP
#define NEMO_INDEX_INDEX_METADATA_HPP

#include "nemo/core/ids.hpp"
#include "nemo/types/type_id.hpp"
#include <vector>
#include <string>

namespace nemo::index {

struct IndexMetadata {
    std::uint32_t index_id;
    std::string name;
    PageID root_page_id;
    std::uint32_t tree_height;
    bool is_unique;
    std::vector<types::TypeID> key_types;
};

} // namespace nemo::index

#endif // NEMO_INDEX_INDEX_METADATA_HPP
