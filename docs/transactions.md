# Nemo Transactions Reference

This guide covers all transaction types, methods, callbacks, and patterns available in Nemo for both synchronous and asynchronous applications.

---

## Transaction Methods Reference

| Target Object | Method / Function | Signature | Description |
| :--- | :--- | :--- | :--- |
| `Connection` | `conn.transaction()` | `() -> Transaction` | Returns an ACID context manager (`with` or `async with`). Commits upon clean exit; rolls back on unhandled exceptions. |
| `Connection` | `conn.begin_transaction()` | `() -> Transaction` | Explicitly starts a new transaction, returning the active `Transaction` handle. |
| `Connection` | `conn.commit()` | `() -> None` | Commits the currently active transaction on this connection. |
| `Connection` | `conn.rollback()` | `() -> None` | Rolls back the active transaction, reverting all modifications made during the transaction. |
| `Connection` | `conn.set_autocommit()` | `(enabled: bool) -> None` | Toggles autocommit mode (`True` by default). When disabled, statements batch into a transaction until `commit()`. |
| `Transaction` | `txn.begin()` | `() -> Transaction` | Starts the transaction (called automatically by context manager or `begin_transaction()`). |
| `Transaction` | `txn.commit()` | `() -> None` | Commits this transaction instance. |
| `Transaction` | `txn.rollback()` | `() -> None` | Rolls back this transaction instance. |
| `Transaction` | `txn.savepoint(name)` | `(name: str) -> None` | Creates a named savepoint checkpoint. |
| `Transaction` | `txn.rollback_to(name)` | `(name: str) -> None` | Rolls back the transaction to the specified savepoint. |
| `Transaction` | `txn.release_savepoint(name)` | `(name: str) -> None` | Discards the savepoint mark and later savepoints. |
| `Connection` | `conn.savepoint(name)` | `(name: str) -> None` | Shortcut to mark a savepoint on the active transaction. |
| `Connection` | `conn.rollback_to(name)` | `(name: str) -> None` | Shortcut to rollback to a savepoint on the connection. |
| `Connection` | `conn.release_savepoint(name)` | `(name: str) -> None` | Shortcut to release a savepoint on the connection. |

---

## All 8 Transaction Patterns and Use Cases

### Pattern 1: Automatic Context Manager Transactions

The recommended Pythonic approach. The context manager automatically commits if the block finishes cleanly, or rolls back if an exception is raised.

#### Synchronous Context Manager
```python
import nemo

with nemo.connect("accounts") as db:
    with db.connect_sync() as conn:
        with conn.transaction():
            conn.execute("UPDATE accounts SET balance = balance - 100 WHERE id = 1;")
            conn.execute("UPDATE accounts SET balance = balance + 100 WHERE id = 2;")
            # Successfully committed on exit
```

#### Asynchronous Context Manager
```python
import asyncio
import nemo

async def transfer():
    with nemo.connect("accounts") as db:
        async with db.connect_async() as conn:
            async with conn.transaction():
                await conn.execute("UPDATE accounts SET balance = balance - 100 WHERE id = 1;")
                await conn.execute("UPDATE accounts SET balance = balance + 100 WHERE id = 2;")

asyncio.run(transfer())
```

---

### Pattern 2: Explicit Manual Transactions

For complex workflows where transaction boundaries span multiple helper functions or conditional steps:

```python
import nemo

with nemo.connect("store") as db:
    with db.connect_sync() as conn:
        txn = conn.begin_transaction()
        try:
            conn.execute("INSERT INTO orders (id, customer) VALUES (?, ?);", (501, "Alice"))
            conn.execute("UPDATE inventory SET stock = stock - 1 WHERE item_id = 99;")
            
            # Commit explicitly
            txn.commit()
        except Exception as e:
            # Revert all changes
            txn.rollback()
            raise
```

---

### Pattern 3: Batch Ingestion with Autocommit Disabled

When executing thousands of single write statements, disabling autocommit groups them into a single transaction commit, drastically increasing write throughput:

```python
import nemo

with nemo.connect("metrics") as db:
    with db.connect_sync() as conn:
        # Disable per-statement autocommit
        conn.set_autocommit(False)
        
        for i in range(10000):
            conn.execute("INSERT INTO sensor_log VALUES (?, ?);", (i, 20.0 + (i % 5)))
        
        # Commit all 10,000 writes at once
        conn.commit()
        
        # Restore autocommit
        conn.set_autocommit(True)
```

---

### Pattern 4: Read-Only Snapshot Isolation

Nemo provides snapshot isolation: readers execute against a consistent snapshot of data without blocking concurrent writers, and writers do not block readers.

```python
import nemo

with nemo.connect("analytics") as db:
    with db.connect_sync() as conn:
        # Long-running analytical query sees consistent point-in-time data
        with conn.transaction():
            total = conn.execute("SELECT SUM(amount) FROM ledger;").fetchone()[0]
            avg = conn.execute("SELECT AVG(amount) FROM ledger;").fetchone()[0]
            print(f"Total: {total}, Average: {avg}")
```

---

### Pattern 5: Multi-Connection Concurrency

Multiple connections can execute transactions simultaneously across threads or coroutines without database-wide lock collisions:

```python
import threading
import nemo

def worker(worker_id: int):
    with nemo.connect("cluster") as db:
        with db.connect_sync() as conn:
            with conn.transaction():
                conn.execute("INSERT INTO worker_heartbeats VALUES (?, ?);", (worker_id, "alive"))

threads = [threading.Thread(target=worker, args=(i,)) for i in range(8)]
for t in threads: t.start()
for t in threads: t.join()
```

---

### Pattern 6: Connection Pool Transactions

When working with connection pools, transactions are contained within the leased connection:

```python
import nemo

with nemo.connect("app") as db:
    pool = db.create_pool_sync(max_connections=4)
    
    with pool.acquire() as conn:
        with conn.transaction():
            conn.execute("INSERT INTO sessions VALUES (?);", ("session_token_xyz",))
            
    pool.close()
```

---

### Pattern 7: Transient Retry Logic

For mission-critical production services, wrap transactions in an exponential backoff retry handler:

```python
import time
import nemo
from nemo import ConcurrencyError, OperationalError

def execute_with_retry(conn, fn, max_attempts=5):
    for attempt in range(max_attempts):
        try:
            with conn.transaction():
                return fn(conn)
        except (ConcurrencyError, OperationalError):
            if attempt == max_attempts - 1:
                raise
            time.sleep(0.01 * (2 ** attempt))
```

---

### Pattern 8: Microsecond Savepoints & Nested Transaction Checkpoints

Savepoints allow speculative execution within a transaction, rolling back erroneous intermediate actions without aborting the parent transaction:

```python
with conn.transaction() as txn:
    conn.execute("INSERT INTO audit_log VALUES ('txn_start');")
    
    # Create checkpoint
    txn.savepoint("checkpoint_1")
    try:
        conn.execute("UPDATE critical_table SET val = ? WHERE id = ?;", (val, row_id))
        if validation_fails:
            # Revert only the speculative update
            txn.rollback_to("checkpoint_1")
    finally:
        txn.release_savepoint("checkpoint_1")
```

---

## Related Runnable Examples
- [`example_56.py`](../example/example_56.py): Microsecond Savepoints & Nested Transactions (`SAVEPOINT`, `ROLLBACK TO SAVEPOINT`, `RELEASE SAVEPOINT`).

- [`example_9.py`](../example/example_9.py): ACID Transactions, Rollbacks, and Multi-Version Concurrency (MVCC).
- [`example_10.py`](../example/example_10.py): Multi-Threaded Connection Pooling and High Concurrency.
- [`example_16.py`](../example/example_16.py): Exact Financial Decimals and Multi-Account Banking Ledgers.
- [`example_39.py`](../example/example_39.py): ACID Transaction Guarantees and Rollback Semantics.


