#ifndef NEMO_CATALOG_CATALOG_ID_HPP
#define NEMO_CATALOG_CATALOG_ID_HPP

#include "nemo/core/ids.hpp"
#include "nemo/core/types.hpp"

namespace nemo::catalog {

using SchemaID = StrongID<struct SchemaIDTag, std::uint32_t>;
using TableID = nemo::TableID;
using ColumnID = nemo::core::ColumnID;
using IndexID = nemo::IndexID;
using ConstraintID = StrongID<struct ConstraintIDTag, std::uint32_t>;

} // namespace nemo::catalog

#endif // NEMO_CATALOG_CATALOG_ID_HPP
