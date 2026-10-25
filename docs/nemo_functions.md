# Nemo Python Functions & API Reference

This document provides a comprehensive technical reference for all module-level functions, classes, and instance methods provided by the **Nemo** Python package (`nemo`).

---

## Table of Contents

1. [Module-Level Functions](#1-module-level-functions)
   - [`nemo.connect()`](#nemoconnect)
   - [`nemo.connect_sync()`](#nemoconnect_sync)
   - [`nemo.connect_async()`](#nemoconnect_async)
   - [`nemo.configure_logging()`](#nemoconfigure_logging)
   - [`nemo.get_logger()`](#nemoget_logger)
2. [Class: `nemo.Database`](#2-class-nemodatabase)
3. [Class: `nemo.SyncConnection`](#3-class-nemosyncconnection)
4. [Class: `nemo.AsyncConnection`](#4-class-nemoasyncconnection)
5. [Classes: `SyncResult` & `AsyncResult`](#5-classes-syncresult--asyncresult)
6. [Classes: `SyncPreparedStatement` & `AsyncPreparedStatement`](#6-classes-syncpreparedstatement--asyncpreparedstatement)
7. [Connection Pooling Classes](#7-connection-pooling-classes)
8. [Exceptions Hierarchy](#8-exceptions-hierarchy)

---

## 1. Module-Level Functions

### `nemo.connect(name_or_path, **kwargs) -> Database`
Opens or creates a Nemo database file (`.nm`) and returns a thread-safe `Database` handle.

- **Parameters**:
  - `name_or_path` (*str*): The database name (e.g. `'production'`, `'analytics'`) or a file system path (e.g. `'/var/data/app.nm'`). If an extension is omitted, `.nm` is appended automatically.
  - `create_if_missing` (*bool*, optional): If `True` (default), creates the database file if it does not already exist.
  - `buffer_pool_size` (*int*, optional): Page cache frame count in memory. Defaults to `1024` (4MB).
- **Returns**: A managed `nemo.Database` instance.
- **Context Manager**: Supports `with nemo.connect(...) as db:` for deterministic closing.

```python
import nemo

with nemo.connect("production") as db:
    with db.connect_sync() as conn:
        conn.execute("CREATE TABLE IF NOT EXISTS inventory (id INT PRIMARY KEY, qty INT);")
```

---

### `nemo.connect_sync(name_or_path, **kwargs) -> SyncConnection`
Direct convenience shortcut that creates or opens a database and immediately acquires a synchronous `SyncConnection`.

```python
import nemo

with nemo.connect_sync("cache") as conn:
    conn.execute("INSERT INTO cache VALUES (?, ?)", ("key_1", "val_1"))
```

---

### `nemo.connect_async(name_or_path, **kwargs) -> AsyncConnection`
Direct convenience shortcut that creates or opens a database and immediately returns an asynchronous `AsyncConnection`.

```python
import asyncio
import nemo

async def main():
    async with nemo.connect_async("events") as conn:
        await conn.execute("INSERT INTO events VALUES (?, ?)", (1, "login"))

asyncio.run(main())
```

---

### `nemo.configure_logging(**kwargs)`
Configures global query logging, performance monitoring, and parameter masking.

- **Keyword Arguments**:
  - `level` (*str*): Logging severity: `'DEBUG'`, `'INFO'`, `'WARNING'`, `'ERROR'`, or `'CRITICAL'`. Default is `'INFO'`.
  - `log_slow_queries` (*bool*): Enables telemetry for slow statements. Default is `False`.
  - `slow_query_threshold_ms` (*float*): Latency threshold in milliseconds. Queries exceeding this trigger warnings. Default is `50.0`.
  - `redact_sensitive_params` (*bool*): Masks potential secrets (`password`, `token`, `secret`, `ssn`) with `[REDACTED]`. Default is `True`.

```python
nemo.configure_logging(
    level="DEBUG",
    log_slow_queries=True,
    slow_query_threshold_ms=10.0,
    redact_sensitive_params=True
)
```

---

### `nemo.get_logger() -> Logger`
Returns the internal Python standard logging instance used by Nemo.

---

## 2. Class: `nemo.Database`

Manages underlying file descriptors, storage pages, WAL logs, and the shared thread-safe engine state.

### Instance Methods

#### `db.connect_sync() -> SyncConnection`
Spawns a new synchronous connection attached to this database. Supports the `with` context manager.

#### `db.connect_async() -> AsyncConnection`
Spawns a new asynchronous connection attached to this database. Supports the `async with` context manager.

#### `db.create_sync_pool(min_connections=1, max_connections=10) -> SyncConnectionPool`
Initializes a thread-safe connection pool for multi-threaded applications.

#### `db.create_async_pool(min_connections=1, max_connections=10) -> AsyncConnectionPool`
Initializes an `asyncio`-native connection pool for high-concurrency event loops.

#### `db.stats() -> dict`
Returns operational metrics:
- `'page_count'`: Total allocated slotted pages.
- `'buffer_pool_hit_rate'`: Percentage of reads served from cache.
- `'wal_lsn'`: Current write-ahead log sequence number.
- `'active_transactions'`: Count of currently uncommitted transactions.

#### `db.close()`
Flushes dirty pages, closes file handles, and releases storage locks.

---

## 3. Class: `nemo.SyncConnection`

The primary interface for synchronous SQL execution and transaction management.

### SQL Execution

#### `conn.execute(sql, parameters=None) -> SyncResult`
Executes a single SQL query or command.
- `sql` (*str*): Valid SQL string.
- `parameters` (*tuple* | *list*, optional): Positional values for `?` placeholders.

```python
res = conn.execute("SELECT * FROM users WHERE score > ?", (50,))
for row in res:
    print(row)
```

#### `conn.executemany(sql, seq_of_parameters) -> SyncResult`
Executes a single SQL statement in a vectorized batch loop over multiple parameter sequences. Yields throughput up to 1M+ rows/sec.

```python
rows = [(1, "A"), (2, "B"), (3, "C")]
conn.executemany("INSERT INTO tbl VALUES (?, ?)", rows)
```

#### `conn.prepare(sql) -> SyncPreparedStatement`
Pre-compiles the query and caches its AST execution plan for fast repeated execution.

---

### High-Velocity Primitives

#### `conn.pop(table, count=1, direction="fifo") -> list[tuple]`
Atomically reads and deletes records from a table in a single lock-free operation directly within C++ engine memory.
- `table` (*str*): Table name acting as queue/stack.
- `count` (*int*): Number of records to remove.
- `direction` (*str*): `'fifo'` (head of table) or `'lifo'` (tail of table).
- **Throughput**: Over **8.5 Million operations/sec**.

```python
# Pop 10 jobs from FIFO queue
batch = conn.pop("job_queue", count=10, direction="fifo")
```

#### `conn.count(table) -> int`
Returns instantaneous row count of `table` in O(1) time using internal slot metadata.

```python
total = conn.count("users")
```

#### `conn.truncate(table)`
Instantly wipes all records and frees pages of `table` in O(1) time without row-by-row deletion.

```python
conn.truncate("session_cache")
```

---

### Analytical & Query Optimizer Methods

#### `conn.analyze(table)`
Computes and updates table-level and column-level cardinality distributions for the Cost-Based Query Optimizer.

```python
conn.analyze("financial_accounts")
```

#### `conn.explain(sql) -> str`
Generates and returns the physical query execution plan tree from the Cost-Based Optimizer.

```python
plan = conn.explain("SELECT AVG(balance) FROM financial_accounts WHERE balance > 1000;")
print(plan)
```

#### `conn.extract_columnar(table, columns) -> dict[str, list]`
High-speed extraction method that directly reads homogeneous columns into Python lists without row-by-row tuple wrapping. Yields up to 152x faster performance than row-based queries.

```python
data = conn.extract_columnar("financial_accounts", ["id", "username", "balance"])
print(data["username"][:5])
```

---

### Sequence & Range Allocation

#### `conn.allocate_id_range(sequence_name, count=1) -> tuple[int, int]`
Atomically advances the sequence by `count` and reserves a private, contiguous range of IDs for the caller.
- **Returns**: `(start_id, end_id)` tuple.
- Completely thread-safe and safe under concurrent distributed writers.

```python
# Reserve 100 IDs for a bulk writer thread
start_id, end_id = conn.allocate_id_range("order_seq", count=100)
# Use IDs from start_id to end_id locally without touching the sequence again!
```

---

### Transaction Controls

#### `conn.begin()`
Manually begins an explicit transaction, disabling autocommit.

#### `conn.commit()`
Atomically persists all changes to the WAL and dirty pages, releasing undo records.

#### `conn.rollback()`
Reverts all modifications performed since the start of the transaction in microseconds.

#### `with conn:` Context Manager
Transactions can be managed automatically using Python context managers:
```python
with conn:
    conn.execute("UPDATE accounts SET balance = balance - 100 WHERE id = 1;")
    conn.execute("UPDATE accounts SET balance = balance + 100 WHERE id = 2;")
    # If any error occurs here, rollback is executed automatically!
```

---

## 4. Class: `nemo.AsyncConnection`

Provides identical capabilities to `SyncConnection`, fully integrated with Python's native `asyncio` event loop.

### Coroutine Methods

| Method Signature | Return Type | Description |
| :--- | :--- | :--- |
| `await conn.execute(sql, params=None)` | `AsyncResult` | Asynchronous query execution. |
| `await conn.executemany(sql, seq)` | `AsyncResult` | Asynchronous batch ingestion. |
| `await conn.prepare(sql)` | `AsyncPreparedStatement` | Asynchronously pre-compiles statement. |
| `await conn.pop(table, count, direction)`| `list[tuple]` | Asynchronous atomic queue/stack pop. |
| `await conn.count(table)` | `int` | Asynchronous O(1) table row count. |
| `await conn.truncate(table)` | `None` | Asynchronous O(1) table truncation. |
| `await conn.allocate_id_range(seq, count)` | `tuple[int, int]` | Asynchronous atomic ID range reservation. |
| `await conn.begin()` | `None` | Asynchronously begins manual transaction. |
| `await conn.commit()` | `None` | Asynchronously commits transaction. |
| `await conn.rollback()` | `None` | Asynchronously rolls back transaction. |
| `await conn.close()` | `None` | Asynchronously closes connection handle. |

### Async Context Manager
```python
async with conn:
    await conn.execute("UPDATE stats SET hits = hits + 1 WHERE id = ?", (42,))
```

---

## 5. Classes: `SyncResult` & `AsyncResult`

Encapsulates rows returned by `SELECT` queries or DML statements.

### Attributes
- `result.columns` (*list[dict]*): Metadata for each returned column (`name`, `type`, `table`, `nullable`).
- `result.column_names` (*list[str]*): List of column label strings.
- `result.rowcount` (*int*): Count of rows affected or returned.
- `result.lastrowid` (*int*): Auto-incremented ID generated by the last `INSERT`.

### Retrieval Methods

#### `result.fetchone() -> tuple | None`
Fetches the next row as a Python tuple, or `None` if exhausted.

#### `result.fetchmany(size=None) -> list[tuple]`
Fetches the next `size` rows. If `size` is omitted, defaults to 100.

#### `result.fetchall() -> list[tuple]`
Fetches all remaining rows as a list of tuples.

### Iteration Protocols

```python
# Synchronous Streaming Iteration
result = conn.execute("SELECT id, name FROM users;")
for uid, name in result:
    print(uid, name)

# Asynchronous Streaming Iteration
async_result = await async_conn.execute("SELECT id, name FROM users;")
async for uid, name in async_result:
    print(uid, name)
```

---

## 6. Classes: `SyncPreparedStatement` & `AsyncPreparedStatement`

Pre-compiled query objects created via `conn.prepare(sql)`.

### Methods
- `stmt.execute(parameters=None) -> Result`: Re-runs the prepared plan with new parameters.
- `stmt.executemany(param_sequences) -> Result`: Runs batch executions against the pre-compiled plan.
- `stmt.close()`: Frees compiled AST structures.

---

## 7. Connection Pooling Classes

### `SyncConnectionPool`
Thread-safe connection pool for multi-threaded applications:
```python
pool = db.create_sync_pool(min_connections=2, max_connections=10)

with pool.acquire() as conn:
    res = conn.execute("SELECT 1;")
```

### `AsyncConnectionPool`
`asyncio`-compatible pool for asynchronous frameworks (FastAPI, Starlette, Sanic, Tornado):
```python
pool = db.create_async_pool(min_connections=5, max_connections=20)

async with pool.acquire() as conn:
    res = await conn.execute("SELECT 1;")
```

---

## 8. Exceptions Hierarchy

All exceptions raised by Nemo inherit from `nemo.NemoError`:

```text
nemo.NemoError
├── DatabaseError
│   ├── TableNotFoundError
│   ├── ColumnNotFoundError
│   ├── DuplicateTableError
│   └── DiskFullError
├── OperationalError
│   ├── LockTimeoutError
│   ├── ConnectionClosedError
│   └── CorruptDatabaseError
├── IntegrityError
│   ├── UniqueConstraintError
│   ├── NotNullConstraintError
│   └── CheckConstraintError
├── ProgrammingError
│   ├── SyntaxError
│   └── TypeMismatchError
└── TimeoutError
```

Every exception includes context attributes: `error.error_code`, `error.sql_state`, and `error.message`.

---

## Related Runnable Examples

- [`example_1.py`](../example/example_1.py): Complete Synchronous Lifecycle (`Database`, `SyncConnection`, `SyncResult`).
- [`example_2.py`](../example/example_2.py): Complete Asynchronous Lifecycle (`AsyncConnection`, `AsyncConnectionPool`).
- [`example_6.py`](../example/example_6.py): Lock-Free Atomic Queue and Stack Popping (`conn.pop()`).
- [`example_7.py`](../example/example_7.py): Sequences and Atomic ID Range Allocation (`allocate_id_range`).
- [`example_8.py`](../example/example_8.py): Direct High-Velocity Columnar Extraction (`conn.extract_columnar()`).
- [`example_9.py`](../example/example_9.py): ACID Transactions, Rollbacks, and MVCC Snapshot Isolation.
- [`example_10.py`](../example/example_10.py): Multi-Threaded Sync Connection Pooling (`SyncConnectionPool`).
- [`example_11.py`](../example/example_11.py): Parameterized Queries and Prepared Statements (`conn.prepare()`).
- [`example_12.py`](../example/example_12.py): Query Execution Statistics and Diagnostics (`result.statistics`).
- [`example_13.py`](../example/example_13.py): Schema Reflection and Column Metadata (`result.columns`).
- [`example_14.py`](../example/example_14.py): Fast Table Truncate and Storage Reclamation (`conn.truncate()`).


