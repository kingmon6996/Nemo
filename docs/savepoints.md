# Nemo Microsecond Savepoints & Nested Transactions

Nemo 0.2.0 introduces **O(1) lightweight transactional savepoints**, providing microsecond-level checkpointing and partial rollback capabilities without copying database files or creating expensive disk checkpoints.

---

## Architectural Principles

Traditional embedded databases implement savepoints by writing heavy journal markers or cloning pages. In contrast, Nemo implements savepoints directly in the connection's in-memory undo buffer:

```
[Transaction Start]
   |
   +--> Op 1: INSERT INTO accounts (Alice)
   |
   +--> [SAVEPOINT sp1] (Records undo_log_offset = 1)
   |       |
   |       +--> Op 2: INSERT INTO accounts (Bob)
   |       +--> Op 3: UPDATE accounts (Charlie)
   |       |
   |       +--> [SAVEPOINT sp2] (Records undo_log_offset = 3)
   |               |
   |               +--> Op 4: INSERT INTO accounts (Dave)
   |
   +--> ROLLBACK TO SAVEPOINT sp1 (Unrolls Op 4, Op 3, Op 2 in memory)
   |
[COMMIT] -> Retains Op 1 (Alice)
```

1. **Lightweight Checkpoint Recording**: `SAVEPOINT name` captures the current undo log offset in O(1) time (< 100 ns).
2. **Sub-Microsecond Unrolling**: `ROLLBACK TO SAVEPOINT name` walks backward from the log tail to the recorded offset, undoing mutated slots in-place.
3. **Duplicate Name Replacement**: Defining a savepoint with an existing name replaces the prior marker and frees superseded undo points.
4. **Nested Scope Release**: `RELEASE SAVEPOINT name` destroys the named savepoint and all savepoints defined subsequent to it.

---

## SQL Grammar Reference

### `SAVEPOINT <name>`
Establishes a named transaction checkpoint:
```sql
SAVEPOINT my_savepoint;
```

### `ROLLBACK TO [SAVEPOINT] <name>`
Reverts all mutations executed after the named savepoint was created:
```sql
ROLLBACK TO SAVEPOINT my_savepoint;
-- Or simply:
ROLLBACK TO my_savepoint;
```

### `RELEASE [SAVEPOINT] <name>`
Discards the named savepoint and any newer savepoints:
```sql
RELEASE SAVEPOINT my_savepoint;
-- Or simply:
RELEASE my_savepoint;
```

---

## Python API Reference

Savepoint operations are available on both `SyncTransaction` / `AsyncTransaction` and `SyncConnection` / `AsyncConnection`:

### Transaction Object Methods

| Method | Signature | Description |
| :--- | :--- | :--- |
| `txn.savepoint(name)` | `(name: str) -> None` | Creates a named savepoint on the active transaction. |
| `txn.rollback_to(name)` | `(name: str) -> None` | Reverts the transaction to the specified savepoint. |
| `txn.release_savepoint(name)`| `(name: str) -> None` | Releases the savepoint and newer savepoints. |

### Connection Object Shortcuts

| Method | Signature | Description |
| :--- | :--- | :--- |
| `conn.savepoint(name)` | `(name: str) -> None` | Creates a savepoint on the current connection transaction. |
| `conn.rollback_to(name)` | `(name: str) -> None` | Reverts the connection to the specified savepoint. |
| `conn.release_savepoint(name)`| `(name: str) -> None` | Releases the savepoint on the connection. |

*(For async connections, all methods are coroutines and should be awaited: `await conn.savepoint(name)`).*

---

## Comprehensive Python Example

```python
import nemo

db = nemo.Database("ledger_db")
conn = db.connect_sync()

conn.execute("CREATE TABLE accounts (id INT PRIMARY KEY, name TEXT, balance DOUBLE);")
conn.execute("INSERT INTO accounts VALUES (1, 'Alice', 1000.0);")
conn.execute("INSERT INTO accounts VALUES (2, 'Bob', 500.0);")

# Pattern: Nested Multi-Level Savepoints with Context Manager
with conn.transaction() as txn:
    # 1. Base operations
    conn.execute("INSERT INTO accounts VALUES (3, 'Charlie', 250.0);")
    
    # 2. Level 1 Checkpoint
    txn.savepoint("step_1")
    conn.execute("INSERT INTO accounts VALUES (4, 'David', 100.0);")
    
    # 3. Level 2 Checkpoint
    txn.savepoint("step_2")
    conn.execute("INSERT INTO accounts VALUES (5, 'Eve', 50.0);")
    
    # Selective Rollback: Rollback to step_2 (Undos Eve, keeps David & Charlie)
    txn.rollback_to("step_2")
    
    # Rollback to step_1 (Undos David, keeps Charlie)
    txn.rollback_to("step_1")
    
    # Release step_1
    txn.release_savepoint("step_1")

# Verify committed state
rows = conn.execute("SELECT id, name FROM accounts;").fetchall()
print("Committed Accounts:", [r[1] for r in rows])
# Output: ['Alice', 'Bob', 'Charlie']
```

