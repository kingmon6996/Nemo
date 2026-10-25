# Synchronous Python Connector Reference

This document provides a complete reference for all classes, methods, and functions in the synchronous connector surface of Nemo (`SyncConnection`, `SyncResult`, `SyncPreparedStatement`, `SyncConnectionPool`, `SyncConnector`, `SyncConnectorPool`).

---

## `SyncConnection`

The `SyncConnection` class represents an active synchronous session to a Nemo database.

### Methods

#### `conn.execute(sql: str, parameters: Optional[Sequence[object]] = None, timeout: float = 0.0) -> SyncResult`
Executes a single SQL statement with optional positional parameter bindings (`?`).
- `sql` (`str`): SQL query string.
- `parameters` (`Sequence[object]`, optional): Tuple or list of parameter values matching `?` placeholders.
- `timeout` (`float`, default `0.0`): Maximum execution time in seconds before raising a `TimeoutError`. `0.0` disables timeout.
- **Returns**: `SyncResult` iterator.

```python
result = conn.execute("SELECT * FROM users WHERE age >= ?;", (18,))
row = result.fetchone()
```

#### `conn.executemany(sql: str, parameters_list: Sequence[Sequence[object]]) -> None`
Executes a parameterized SQL statement repeatedly across an iterable of parameter sequences using high-throughput batching.
- `sql` (`str`): SQL statement with placeholders.
- `parameters_list` (`Sequence[Sequence[object]]`): Sequence of tuples/lists containing parameter values.

```python
records = [
    (1, "Alice", 100.0),
    (2, "Bob", 250.5),
    (3, "Charlie", 75.25),
]
conn.executemany("INSERT INTO accounts VALUES (?, ?, ?);", records)
```

#### `conn.prepare(sql: str) -> SyncPreparedStatement`
Pre-compiles a SQL query for repeated execution with different parameters, minimizing query parsing overhead.
- `sql` (`str`): Parameterized SQL query.
- **Returns**: `SyncPreparedStatement`.

```python
stmt = conn.prepare("INSERT INTO logs (level, message) VALUES (?, ?);")
stmt.execute("INFO", "Application boot")
stmt.execute("WARN", "High memory usage")
```

#### `conn.pop(table: str, count: int = 1, direction: str = "fifo") -> list[tuple]`
Atomically removes and returns rows from a table as an atomic queue or stack operation directly inside the C++ engine.
- `table` (`str`): Name of the table to pop from.
- `count` (`int`, default `1`): Number of rows to pop.
- `direction` (`str`, default `"fifo"`):
  - `"fifo"`: Pops the oldest rows from the head of the table (queue behavior).
  - `"lifo"`: Pops the newest rows from the tail of the table (stack behavior).
- **Returns**: List of popped row tuples, each in the form `(row_id, value)`.

```python
# Pop 5 oldest jobs from queue (FIFO)
tasks = conn.pop("job_queue", count=5, direction="fifo")
for row_id, payload in tasks:
    print(f"Processing job {row_id}: {payload}")

# Pop 1 most recent audit event (LIFO)
last_event = conn.pop("audit_trail", count=1, direction="lifo")
```

#### `conn.count(table: str) -> int`
Returns the total row count of a table.
- `table` (`str`): Target table name.
- **Returns**: Total number of rows as an integer.

```python
total_users = conn.count("users")
```

#### `conn.truncate(table: str) -> int`
Empties all rows from the specified table.
- `table` (`str`): Target table name.
- **Returns**: Number of rows deleted.

```python
deleted_count = conn.truncate("temporary_cache")
```

#### `conn.transaction() -> SyncTransaction`
Returns an ACID transaction context manager for use in a `with` statement.
- Automatically commits if the block finishes without unhandled exceptions.
- Automatically rolls back if an unhandled exception is raised.

```python
with conn.transaction():
    conn.execute("UPDATE accounts SET balance = balance - 50 WHERE id = 1;")
    conn.execute("UPDATE accounts SET balance = balance + 50 WHERE id = 2;")
```

#### `conn.begin_transaction() -> SyncTransaction`
Explicitly starts a transaction and returns the active `SyncTransaction` object.

```python
txn = conn.begin_transaction()
try:
    conn.execute("INSERT INTO orders VALUES (?, ?);", (101, "pending"))
    conn.commit()
except Exception:
    conn.rollback()
    raise
```

#### `conn.commit() -> None`
Explicitly commits the current active transaction on this connection.

#### `conn.rollback() -> None`
Explicitly rolls back the current active transaction on this connection.

#### `conn.set_autocommit(enabled: bool) -> None`
Enables or disables automatic commit mode.
- `enabled = True` (default): Each statement commits immediately upon execution.
- `enabled = False`: Statements remain in an active transaction until `conn.commit()` is called.

```python
conn.set_autocommit(False)
conn.execute("INSERT INTO audit VALUES ('entry 1');")
conn.execute("INSERT INTO audit VALUES ('entry 2');")
conn.commit()
conn.set_autocommit(True)
```

#### `conn.analyze(table: str) -> None`
Triggers the Cost-Based Optimizer's statistical analysis on the specified table, computing row counts, distribution cardinality, and data sizes.

```python
conn.analyze("financial_accounts")
```

#### `conn.explain(sql: str) -> str`
Generates the Cost-Based Optimizer physical query plan tree for the specified SQL query.

```python
plan = conn.explain("SELECT AVG(balance) FROM financial_accounts WHERE balance > 1000;")
print(plan)
```

#### `conn.extract_columnar(table: str, columns: Sequence[str]) -> dict[str, list]`
Directly extracts columns as homogeneous columnar arrays, bypassing row-by-row tuple wrapping for maximum analytical throughput.

```python
data = conn.extract_columnar("financial_accounts", ["id", "username", "balance"])
print("IDs:", data["id"][:5])
print("Usernames:", data["username"][:5])
```
*Performance: Over **1,980 extractions/second** (152.75x faster than SQLite).*

#### `conn.allocate_id_range(sequence_name: str, count: int) -> tuple[int, int]`
Atomically reserves a contiguous block of unique sequential IDs from a sequence for high-throughput batch writer workers.

```python
start_id, end_id = conn.allocate_id_range("order_id_seq", count=1000)
print(f"Worker reserved range: [{start_id}, {end_id}]")
```

#### `conn.cancel() -> None`
Cancels the currently running query on this connection.

#### `conn.close() -> None`
Closes the connection and releases its worker lease back to the pool.

---

## `SyncResult`

`SyncResult` is returned by `execute()` and represents the cursor over query results. It implements the standard Python iterator protocol.

### Methods

#### `result.fetchone() -> Optional[tuple]`
Fetches the next row in the result set, or returns `None` if no more rows are available.

```python
row = result.fetchone()
if row:
    print("User ID:", row[0], "Name:", row[1])
```

#### `result.fetchmany(size: int) -> list[tuple]`
Fetches up to `size` rows from the result set.
- `size` (`int`): Maximum number of rows to retrieve.
- **Returns**: List of row tuples.

```python
batch = result.fetchmany(50)
```

#### `result.fetchall() -> list[tuple]`
Fetches all remaining rows in the result set.

```python
all_rows = result.fetchall()
```

#### `for row in result:`
`SyncResult` supports direct iteration:

```python
result = conn.execute("SELECT id, username FROM users;")
for row in result:
    print(row)
```

### Properties

- `result.affected_rows` (`int`): Number of rows modified, inserted, or deleted by DML queries (`INSERT`, `UPDATE`, `DELETE`).
- `result.has_rows` (`bool`): `True` if the result set contains rows to read, `False` otherwise.
- `result.columns` (`list`): List of column names and datatype descriptors.
- `result.statistics` (`dict`): Query execution metrics, including execution duration in microseconds and rows scanned.

---

## `SyncPreparedStatement`

A pre-compiled SQL statement for repeated execution.

### Methods

#### `stmt.execute(*parameters: object) -> SyncResult`
Executes the prepared query with positional parameters.

```python
stmt = conn.prepare("SELECT * FROM items WHERE category = ? AND in_stock = ?;")
res1 = stmt.execute("electronics", True)
res2 = stmt.execute("books", True)
```

---

## `SyncConnectionPool`

Thread-safe pool managing multiple `SyncConnection` instances.

### Methods

#### `pool.acquire() -> SyncConnection`
Acquires an available connection from the pool.

```python
conn = pool.acquire()
try:
    conn.execute("SELECT 1;")
finally:
    conn.close()
```

#### `pool.connection()`
Alias for `pool.acquire()`.

#### `pool.close() -> None`
Closes all pooled connections and invalidates the pool.

#### Context Manager Usage
```python
with db.create_pool_sync(max_connections=8) as pool:
    with pool.acquire() as conn:
        conn.execute("SELECT 1;")
```

---

## Related Runnable Examples

- [`example_1.py`](../example/example_1.py): Basic Synchronous CRUD Operations and Primary Keys.
- [`example_6.py`](../example/example_6.py): Fast Queue and Stack Popping (`conn.pop()`).
- [`example_7.py`](../example/example_7.py): Sequences and Atomic ID Range Allocation (`allocate_id_range`).
- [`example_8.py`](../example/example_8.py): Direct High-Velocity Columnar Extraction (`conn.extract_columnar()`).
- [`example_9.py`](../example/example_9.py): ACID Transactions, Rollbacks, and MVCC Snapshot Isolation.
- [`example_10.py`](../example/example_10.py): Multi-Threaded Sync Connection Pooling (`SyncConnectionPool`).
- [`example_33.py`](../example/example_33.py): High-Throughput FIFO Task Queue Benchmarks.
- [`example_37.py`](../example/example_37.py): Direct Columnar Extraction for Analytical Pipelines.


