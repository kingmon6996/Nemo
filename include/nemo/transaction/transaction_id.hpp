#ifndef NEMO_TRANSACTION_TRANSACTION_ID_HPP
#define NEMO_TRANSACTION_TRANSACTION_ID_HPP

#include <cstdint>
#include <functional>

namespace nemo::transaction {

using TransactionID = std::uint64_t;

constexpr TransactionID INVALID_TRANSACTION_ID = 0;
constexpr TransactionID INITIAL_TRANSACTION_ID = 1;

} // namespace nemo::transaction

#endif // NEMO_TRANSACTION_TRANSACTION_ID_HPP
