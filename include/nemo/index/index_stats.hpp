#ifndef NEMO_INDEX_INDEX_STATS_HPP
#define NEMO_INDEX_INDEX_STATS_HPP

#include <cstdint>

namespace nemo::index {

struct IndexStats {
    std::uint32_t height{0};
    std::uint32_t node_count{0};
    std::uint32_t leaf_count{0};
    std::uint32_t internal_node_count{0};
    std::uint64_t entry_count{0};
    std::uint32_t split_count{0};
    std::uint32_t merge_count{0};
    double average_leaf_occupancy{0.0};
    double average_internal_occupancy{0.0};
};

} // namespace nemo::index

#endif // NEMO_INDEX_INDEX_STATS_HPP
