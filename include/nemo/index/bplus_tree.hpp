#ifndef NEMO_INDEX_BPLUS_TREE_HPP
#define NEMO_INDEX_BPLUS_TREE_HPP

#include "nemo/index/index_key.hpp"
#include "nemo/index/index_metadata.hpp"
#include "nemo/storage/buffer_pool.hpp"
#include "nemo/storage/record_id.hpp"
#include "nemo/core/status.hpp"
#include <vector>
#include <memory>

namespace nemo::index {

class BPlusTreeIterator; // Forward decl

class BPlusTree {
public:
    BPlusTree(storage::BufferPool* buffer_pool, IndexMetadata metadata);

    // Creates a new empty tree and allocates its metadata.
    static Result<std::unique_ptr<BPlusTree>> create(storage::BufferPool* buffer_pool, const IndexMetadata& meta);

    // Load an existing tree from metadata
    static std::unique_ptr<BPlusTree> open(storage::BufferPool* buffer_pool, const IndexMetadata& meta);

    const IndexMetadata& metadata() const { return m_metadata; }

    // Core operations
    Result<void> insert(const IndexKey& key, storage::RecordID record_id);
    
    // Returns true if deleted, false if not found
    Result<bool> remove(const IndexKey& key, storage::RecordID record_id);
    
    // Returns all exact matches
    Result<std::vector<storage::RecordID>> find(const IndexKey& key);
    
    // Returns iterator pointing to first entry >= key
    Result<BPlusTreeIterator> lower_bound(const IndexKey& key);
    
    // Returns iterator pointing to first entry > key
    Result<BPlusTreeIterator> upper_bound(const IndexKey& key);
    
    // Returns an iterator at the beginning of the tree
    Result<BPlusTreeIterator> begin();

private:
    storage::BufferPool* m_buffer_pool;
    IndexMetadata m_metadata;

    // Helper functions
    bool is_empty() const;
    void start_new_tree(const IndexKey& key, storage::RecordID record_id);
    
    // Insert into a leaf node and handle splits
    Result<void> insert_into_leaf(PageID leaf_id, const IndexKey& key, storage::RecordID record_id);
    
    // Insert into a parent node after a split
    Result<void> insert_into_parent(PageID left_id, const IndexKey& separator, PageID right_id);
    
    // Recursive search to find the leaf containing (or that should contain) the key
    Result<PageID> find_leaf(const IndexKey& key);
};

} // namespace nemo::index

#endif // NEMO_INDEX_BPLUS_TREE_HPP
