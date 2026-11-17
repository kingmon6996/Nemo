# Concurrency — Read-Committed (Phase 5)

Nemo uses a **Read-Committed** isolation model with per-table `shared_mutex` locking and per-row atomic visibility flags. This avoids the complexity and overhead of full MVCC while still preventing dirty reads.

---

## Isolation Level

| Level | Dirty Reads | Non-Repeatable Reads | Phantom Reads |
|-------|------------|---------------------|---------------|
| Read-Committed ✅ | **Prevented** | Allowed | Allowed |

Nemo does **not** implement Snapshot Isolation or Serializable. Use explicit transactions to control read scope.

---

## Per-Row Visibility Flags

Each row in `TableData` has a parallel `std::atomic<uint8_t>` flag vector (`row_flags`):

| Bit | Mask | Meaning |
|-----|------|---------|
| 0 | `0x01` | **committed** — row is visible to readers |
| 1 | `0x02` | **deleted** — row is soft-deleted, invisible to readers |

### Insert lifecycle

```
INSERT (auto-commit)        → push_row(row, committed=true)   flags = 0x01  ← visible immediately
INSERT (inside BEGIN...;)   → push_row(row, committed=false)  flags = 0x00  ← invisible until COMMIT
COMMIT                      → commit_rows(from_idx)           flags |= 0x01 ← visible
ROLLBACK                    → delete_rows(from_idx)           flags |= 0x02 ← permanently invisible
```

Readers evaluate `is_row_visible(idx)`:
```cpp
bool is_row_visible(std::size_t idx) const noexcept {
    uint8_t f = row_flags[idx].load(std::memory_order_acquire);
    return (f & 0x01) && !(f & 0x02);
}
```

Rows that pre-date Phase 5 (loaded from disk or no flag entry) are treated as committed.

---

## Per-Table Locking

`TableData` has two mutexes:

| Mutex | Type | Who holds it |
|-------|------|-------------|
| `mutex` | `std::shared_mutex` | Writers: `unique_lock`; Readers: `shared_lock` |
| `write_mutex` | `std::mutex` | Serializes concurrent writers from different connections |

### Reader path (SELECT)
```cpp
std::shared_lock<std::shared_mutex> r_lk(tbl->mutex);
// iterate sample_rows; skip !is_row_visible(i)
```

### Writer path (INSERT)
```cpp
std::unique_lock<std::shared_mutex> w_lk(tbl->mutex);
tbl->push_row(row_vals, !m_impl->in_transaction);
```

Multiple readers hold `shared_lock` simultaneously (zero-wait). A writer acquires `unique_lock`, which blocks until all readers have released their `shared_lock`s. The hold time for a single-row INSERT is typically < 1 µs.

---

## Transaction-Scoped Pending Rows

When a connection calls `BEGIN`, the engine snapshots the current row count in every table into `ConnectionImpl::pending_row_starts`. On `COMMIT` or `ROLLBACK`, only rows with index ≥ `from_idx` are stamped:

```
BEGIN     → pending_row_starts[tbl] = tbl->sample_rows.size()
INSERT    → push_row(row, committed=false)   // flags[new_idx] = 0x00
COMMIT    → commit_rows(from_idx)           // flags[from_idx..] |= 0x01
ROLLBACK  → delete_rows(from_idx)           // flags[from_idx..] |= 0x02
```

This is a constant-time O(1) snapshot + O(k) stamp (k = rows inserted in this txn).

---

## Process Lock File (`.nemo/<name>/lock`)

An advisory OS-level file lock is written to `.nemo/<name>/lock` on `nemo.connect()`:

```python
# Windows
msvcrt.locking(fd, msvcrt.LK_NBLCK, 1)

# Linux / macOS
fcntl.flock(fd, LOCK_EX | LOCK_NB)
```

- The file contains the PID of the opening process.
- It is removed by `Database.close()` / `__exit__`.
- **Non-fatal**: if locking fails (e.g. different filesystem, permissions), the database still opens — the lock is advisory only.
- **Threads within one process** share the lock safely; only a *second process* attempting to open the same DB would need to check the lock.

```
Process A: nemo.connect("mydb") → writes .nemo/mydb/lock (PID=1234)
Process B: nemo.connect("mydb") → lock file already held by PID=1234
                                  (second process can detect this via lock file)
Process A: db.close()           → removes .nemo/mydb/lock
```

---

## `compact()` / VACUUM

Soft-deleted rows accumulate in `sample_rows` and are skipped by readers. `TableData::compact()` purges them:

```cpp
void compact() {
    // moves visible rows down, trims sample_rows + row_flags
}
```

This is called implicitly by `checkpoint()` in a future maintenance pass. Until compaction, deleted rows consume memory but are invisible.

---

## `persist_state()` Filtering

When flushing to `data.nm`, only rows where `is_row_visible(i)` is `true` are written to disk. Uncommitted and deleted rows are never persisted, so they cannot appear after a reopen.

---

## Concurrency Summary

| Scenario | Behaviour |
|----------|-----------|
| Multiple readers | Concurrent `shared_lock` → zero contention |
| Reader + writer | Writer waits for readers to finish, then takes `unique_lock` |
| Multiple writers (same table) | Serialized via `unique_lock<shared_mutex>` |
| Uncommitted INSERT visible to other connections | **No** — flags=0x00 until COMMIT |
| Rolled-back INSERT visible after reopen | **No** — only `is_row_visible()` rows are persisted |
| Second process opening same DB | Advisory lock file warns; non-fatal if ignored |

