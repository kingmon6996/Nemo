#include "nemo/wal/checkpoint_state.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace nemo::wal {
namespace {

constexpr std::uint64_t CHECKPOINT_STATE_MAGIC = 0x31504B434F4D454EULL;
constexpr std::uint32_t CHECKPOINT_STATE_VERSION = 1;
constexpr std::size_t CHECKPOINT_STATE_HEADER_SIZE = 28;
constexpr std::size_t TRANSACTION_ENTRY_SIZE = 24;
constexpr std::size_t DIRTY_PAGE_ENTRY_SIZE = 16;

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
    }
}

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
    }
}

std::uint32_t read_u32(const std::uint8_t* bytes) {
    std::uint32_t value = 0;
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(bytes[shift / 8]) << shift;
    }
    return value;
}

std::uint64_t read_u64(const std::uint8_t* bytes) {
    std::uint64_t value = 0;
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes[shift / 8]) << shift;
    }
    return value;
}

} // namespace

std::vector<std::uint8_t> serialize_checkpoint_state(const CheckpointState& state) {
    if (state.transactions.size() > std::numeric_limits<std::uint32_t>::max() ||
        state.dirty_pages.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("Checkpoint state contains too many entries.");
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(
        CHECKPOINT_STATE_HEADER_SIZE +
        state.transactions.size() * TRANSACTION_ENTRY_SIZE +
        state.dirty_pages.size() * DIRTY_PAGE_ENTRY_SIZE);
    append_u64(payload, CHECKPOINT_STATE_MAGIC);
    append_u32(payload, CHECKPOINT_STATE_VERSION);
    append_u64(payload, state.max_transaction_id);
    append_u32(payload, static_cast<std::uint32_t>(state.transactions.size()));
    append_u32(payload, static_cast<std::uint32_t>(state.dirty_pages.size()));

    auto transactions = state.transactions;
    std::sort(transactions.begin(), transactions.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.transaction_id < rhs.transaction_id;
        });
    for (const auto& transaction : transactions) {
        append_u64(payload, transaction.transaction_id);
        append_u64(payload, transaction.first_lsn);
        append_u64(payload, transaction.last_lsn);
    }

    auto dirty_pages = state.dirty_pages;
    std::sort(dirty_pages.begin(), dirty_pages.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.page_id < rhs.page_id;
        });
    for (const auto& page : dirty_pages) {
        append_u64(payload, page.page_id);
        append_u64(payload, page.rec_lsn);
    }
    return payload;
}

std::optional<CheckpointState> deserialize_checkpoint_state(
    const std::vector<std::uint8_t>& payload) {
    if (payload.size() < CHECKPOINT_STATE_HEADER_SIZE ||
        read_u64(payload.data()) != CHECKPOINT_STATE_MAGIC ||
        read_u32(payload.data() + 8) != CHECKPOINT_STATE_VERSION) {
        return std::nullopt;
    }

    const auto transaction_count = read_u32(payload.data() + 20);
    const auto dirty_page_count = read_u32(payload.data() + 24);
    const auto expected_size =
        CHECKPOINT_STATE_HEADER_SIZE +
        static_cast<std::uint64_t>(transaction_count) * TRANSACTION_ENTRY_SIZE +
        static_cast<std::uint64_t>(dirty_page_count) * DIRTY_PAGE_ENTRY_SIZE;
    if (expected_size != payload.size()) return std::nullopt;

    CheckpointState state;
    state.max_transaction_id = read_u64(payload.data() + 12);
    state.transactions.reserve(transaction_count);
    state.dirty_pages.reserve(dirty_page_count);

    std::size_t offset = CHECKPOINT_STATE_HEADER_SIZE;
    for (std::uint32_t index = 0; index < transaction_count; ++index) {
        CheckpointTransaction transaction{
            read_u64(payload.data() + offset),
            read_u64(payload.data() + offset + 8),
            read_u64(payload.data() + offset + 16)};
        if (transaction.transaction_id == 0 || transaction.first_lsn == 0 ||
            transaction.last_lsn < transaction.first_lsn) {
            return std::nullopt;
        }
        state.transactions.push_back(transaction);
        offset += TRANSACTION_ENTRY_SIZE;
    }

    for (std::uint32_t index = 0; index < dirty_page_count; ++index) {
        CheckpointDirtyPage page{
            read_u64(payload.data() + offset),
            read_u64(payload.data() + offset + 8)};
        if (page.rec_lsn == 0) return std::nullopt;
        state.dirty_pages.push_back(page);
        offset += DIRTY_PAGE_ENTRY_SIZE;
    }
    return state;
}

} // namespace nemo::wal
