#include "nemo/index/bplus_tree.hpp"
#include "nemo/index/bplus_tree_iterator.hpp"
#include "nemo/index/bplus_tree_internal.hpp"
#include "nemo/index/index_comparator.hpp"

namespace nemo::index {

BPlusTreeIterator::BPlusTreeIterator(storage::BufferPool* buffer_pool, PageID leaf_id, std::uint16_t index, const std::vector<types::TypeID>& types)
    : m_buffer_pool(buffer_pool), m_current_page_id(leaf_id), m_current_index(index), m_types(types) {
    load_page();
}

void BPlusTreeIterator::load_page() {
    if (!m_current_page_id.is_valid()) {
        m_leaf_cache.reset();
        return;
    }
    
    auto handle_res = m_buffer_pool->fetch_page(m_current_page_id);
    if (!handle_res) {
        m_current_page_id = PageID{};
        m_leaf_cache.reset();
        return;
    }
    
    m_leaf_cache = std::make_unique<BPlusTreeLeaf>(std::move(handle_res.value()));
    
    // Check if index is valid
    if (m_current_index >= m_leaf_cache->key_count()) {
        m_current_page_id = m_leaf_cache->next_page_id();
        m_current_index = 0;
        load_page();
    }
}

bool BPlusTreeIterator::is_valid() const {
    return m_leaf_cache != nullptr && m_current_index < m_leaf_cache->key_count();
}

void BPlusTreeIterator::next() {
    if (!is_valid()) return;
    
    m_current_index++;
    m_key_cached = false;
    
    if (m_current_index >= m_leaf_cache->key_count()) {
        m_current_page_id = m_leaf_cache->next_page_id();
        m_current_index = 0;
        load_page();
    }
}

const IndexKey& BPlusTreeIterator::key() const {
    if (!m_key_cached && is_valid()) {
        auto kb = m_leaf_cache->key_bytes_at(m_current_index);
        m_current_key_cache = IndexKey::deserialize(kb, m_types);
        m_key_cached = true;
    }
    return m_current_key_cache;
}

storage::RecordID BPlusTreeIterator::record_id() const {
    if (is_valid()) {
        return m_leaf_cache->record_id_at(m_current_index);
    }
    return storage::RecordID();
}

Result<BPlusTreeIterator> BPlusTree::lower_bound(const IndexKey& key) {
    if (is_empty()) return BPlusTreeIterator();
    
    auto leaf_id_res = find_leaf(key);
    if (!leaf_id_res) return leaf_id_res.error();
    
    auto handle_res = m_buffer_pool->fetch_page(leaf_id_res.value());
    if (!handle_res) return handle_res.error();
    
    BPlusTreeLeaf leaf(std::move(handle_res.value()));
    auto [idx, exact] = leaf.find_lower_bound(key);
    
    return BPlusTreeIterator(m_buffer_pool, leaf.page_id(), idx, m_metadata.key_types);
}

Result<BPlusTreeIterator> BPlusTree::upper_bound(const IndexKey& key) {
    if (is_empty()) return BPlusTreeIterator();
    
    auto leaf_id_res = find_leaf(key);
    if (!leaf_id_res) return leaf_id_res.error();
    
    auto handle_res = m_buffer_pool->fetch_page(leaf_id_res.value());
    if (!handle_res) return handle_res.error();
    
    BPlusTreeLeaf leaf(std::move(handle_res.value()));
    auto [idx, exact] = leaf.find_lower_bound(key);
    
    // advance until we are strictly greater
    BPlusTreeIterator it(m_buffer_pool, leaf.page_id(), idx, m_metadata.key_types);
    
    while (it.is_valid() && IndexComparator::compare(it.key(), key) <= 0) {
        it.next();
    }
    
    return it;
}

Result<BPlusTreeIterator> BPlusTree::begin() {
    if (is_empty()) return BPlusTreeIterator();
    
    PageID curr_id = m_metadata.root_page_id;
    
    while (true) {
        auto handle_res = m_buffer_pool->fetch_page(curr_id);
        if (!handle_res) return handle_res.error();
        
        BPlusTreeNode node(std::move(handle_res.value()));
        
        if (node.is_leaf()) {
            return BPlusTreeIterator(m_buffer_pool, curr_id, 0, m_metadata.key_types);
        } else {
            BPlusTreeInternal internal(std::move(node).handle());
            curr_id = internal.child_at(0); // Leftmost child
        }
    }
}

} // namespace nemo::index

