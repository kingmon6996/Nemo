#ifndef NEMO_WAL_DIRTY_PAGE_TABLE_HPP
#define NEMO_WAL_DIRTY_PAGE_TABLE_HPP

#include "nemo/core/ids.hpp"
#include "nemo/wal/lsn.hpp"
#include <unordered_map>

namespace nemo::wal {

class DirtyPageTable {
public:
    // Update the recLSN (the LSN of the first log record that made this page dirty after a checkpoint)
    void update(PageID page_id, LSN rec_lsn) {
        auto it = m_table.find(page_id);
        if (it == m_table.end()) {
            m_table[page_id] = rec_lsn;
        }
    }
    
    void remove(PageID page_id) {
        m_table.erase(page_id);
    }
    
    LSN get_redo_start_lsn() const {
        LSN min_lsn = LSN::max();
        for (const auto& [id, lsn] : m_table) {
            if (lsn.value() < min_lsn.value()) {
                min_lsn = lsn;
            }
        }
        return min_lsn;
    }
    
    bool contains(PageID page_id) const {
        return m_table.find(page_id) != m_table.end();
    }
    
    LSN get_rec_lsn(PageID page_id) const {
        auto it = m_table.find(page_id);
        if (it != m_table.end()) return it->second;
        return LSN::invalid();
    }

private:
    std::unordered_map<PageID, LSN> m_table;
};

} // namespace nemo::wal

#endif // NEMO_WAL_DIRTY_PAGE_TABLE_HPP
