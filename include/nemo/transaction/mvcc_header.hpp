#ifndef NEMO_TRANSACTION_MVCC_HEADER_HPP
#define NEMO_TRANSACTION_MVCC_HEADER_HPP

#include "nemo/transaction/transaction_id.hpp"
#include "nemo/storage/record_id.hpp"

namespace nemo::transaction {

// Represents the MVCC metadata prepended/attached to each physical record.
// If xmax is INVALID_TRANSACTION_ID, the row is not deleted/updated.
// prev_version points to the old version in an Old-to-New chaining model.
struct MVCCHeader {
    TransactionID xmin{INVALID_TRANSACTION_ID};
    TransactionID xmax{INVALID_TRANSACTION_ID};
    storage::RecordID prev_version{};
};

} // namespace nemo::transaction

#endif // NEMO_TRANSACTION_MVCC_HEADER_HPP
