#ifndef NEMO_CATALOG_SCHEMA_HPP
#define NEMO_CATALOG_SCHEMA_HPP

#include "nemo/catalog/catalog_id.hpp"
#include <string>

namespace nemo::catalog {

struct SchemaMetadata {
    SchemaID schema_id;
    std::string name;
};

} // namespace nemo::catalog

#endif // NEMO_CATALOG_SCHEMA_HPP
