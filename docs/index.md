# B+ Tree Index — Phase 6

## Overview

Phase 6 adds a **secondary index layer** to Nemo that accelerates equality-predicate
`SELECT` queries from O(N) table scans to O(log N) point lookups.

The index structure used in Nemo's in-memory engine is `InMemoryIndex` — a
`std::map`-backed ordered structure — which mirrors the logical semantics of a
disk-primary B+ Tree while running entirely in RAM.  When the disk-primary engine
is activated (via `BufferPool`), the existing `BPlusTree` headers at
`include/nemo/index/` provide a page-aware equivalent with the same interface.

---

## SQL Syntax

```sql
-- Create a secondary index
CREATE INDEX idx_name ON table_name(column_name);

-- Create a unique index
CREATE UNIQUE INDEX idx_email ON users(email);

-- Drop an index (IF EXISTS is optional)
DROP INDEX IF EXISTS idx_name;
```

---

## Internal Architecture

### `ValueLess` comparator (`api_internal.hpp`)

```cpp
struct ValueLess {
    bool operator()(const types::Value& a, const types::Value& b) const;
};
```

Provides a total ordering over `types::Value` suitable for use as a `std::map`
comparator.  Numeric values are compared as `double`; all others fall back to
their `to_string()` lexicographic representation.

### `InMemoryIndex` struct (`api_internal.hpp`)

```
index_name   — unique name registered with SharedDatabaseState::indexes
table_name   — lower-cased table name
column_name  — lower-cased indexed column name
col_idx      — zero-based index into TableData::columns / sample_rows
is_unique    — set by CREATE UNIQUE INDEX
entries      — std::map<Value, vector<row_idx>, ValueLess>
mutex        — shared_mutex protecting entries
```

Key methods:

| Method | Description |
|--------|-------------|
| `rebuild(tbl)` | Full rebuild from committed visible rows; called on `CREATE INDEX` and after destructive `DELETE` |
| `on_insert(key, row_idx)` | O(log N) insert; called after every `push_row()` |
| `on_delete(key, row_idx)` | O(log N) removal of a single row slot |
| `lookup(key)` | Shared-locked point lookup returning matching row indices |

### `SharedDatabaseState` helpers

```cpp
// Retrieve the first index covering (table, column); nullptr if none
shared_ptr<InMemoryIndex> get_index(tbl_lower, col_lower);

// Notify all indexes on tbl_lower that a new row was appended
void index_on_insert(tbl_lower, row_idx, row);

// Notify all indexes on tbl_lower that a row was logically deleted
void index_on_delete(tbl_lower, row_idx, row);
```

---

## Insert Notification Path

After every call to `TableData::push_row()` in `connection.cpp`, the new row's
index position (`tbl->sample_rows.size() - 1`) is forwarded to
`SharedDatabaseState::index_on_insert()`.  This covers all five INSERT code
paths:

1. Template fast-path (cached integer-ID inserts)
2. Regular fast INSERT path
3. Full query-engine INSERT path
4. `INSERT INTO … SELECT …`
5. `INSERT … ON CONFLICT … DO NOTHING / DO UPDATE`

---

## SELECT WHERE Fast Path

For simple equality predicates (`WHERE col = val`) where `col` is not the
primary key `id`, Nemo checks whether a live `InMemoryIndex` covers that
column:

```
parse_simple_where()   →  WherePredicate{column, op="=", value}
get_index(tbl, col)    →  InMemoryIndex* or nullptr
idx->lookup(key)       →  vector<row_idx>
filter by is_row_visible()
return matching rows
```

If no index exists the query falls through to `execute_extraction_query()` for
the normal linear scan.

---

## DELETE Invalidation Strategy

Nemo uses a **rebuild-on-delete** strategy:

| Delete type | Action |
|-------------|--------|
| Full-table `DELETE` (no WHERE) | Clear all index `entries` maps for the table |
| `DELETE WHERE id = N` | Rebuild affected indexes after the slot is cleared in-place |
| `DELETE WHERE col = val` | Rebuild affected indexes after `sample_rows` is compacted |

Rebuild is preferred over surgical `on_delete` calls on the WHERE path because
`std::remove_if` / `erase` shifts row indices, invalidating all stored positions.

---

## DROP TABLE Cascade

`DROP TABLE` now also erases every `InMemoryIndex` whose `table_name` matches
the dropped table, preventing stale lookups if the table is recreated.

---

## Uniqueness Enforcement

`CREATE UNIQUE INDEX` sets `InMemoryIndex::is_unique = true`.  Violation
detection is performed at the query layer; the index itself does not enforce
uniqueness — it is the caller's responsibility to check `idx->lookup(key).empty()`
before inserting when `is_unique` is set.

---

## Limitations (current phase)

- Only **equality** predicates benefit from the index fast path; range scans
  (`>`, `<`, `BETWEEN`) still use the linear scan / `execute_extraction_query`.
- Index state is **in-memory only**: it is rebuilt from `data.nm` on database
  open (the `rebuild()` method is called during `CREATE INDEX`; there is no
  persistent index file yet).
- The disk-primary `BPlusTree` at `include/nemo/index/bplus_tree.hpp` is wired
  for a future phase and is not activated here.

