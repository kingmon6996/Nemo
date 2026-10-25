# Asynchronous Python Connector Reference (`asyncio`)

This document provides a complete reference for all asynchronous classes, methods, and coroutines in Nemo (`AsyncConnection`, `AsyncResult`, `AsyncPreparedStatement`, `AsyncConnectionPool`, `AsyncConnector`, `AsyncConnectorPool`).

---

## `AsyncConnection`

The `AsyncConnection` class provides non-blocking, coroutine-based database operations integrated with Python's standard `asyncio` event loop.

### Coroutines & Methods

#### `await conn.execute(sql: str, parameters: Optional[Sequence[object]] = None, timeout: float = 0.0) -> AsyncResult`
Asynchronously executes a single SQL query.
- `sql` (`str`): SQL statement string.
- `parameters` (`Sequence[object]`, optional): Tuple or list of values matching `?` placeholders.
- `timeout` (`float`, default `0.0`): Timeout in seconds. Raises `TimeoutError` if exceeded.
- **Returns**: `AsyncResult` cursor.

```python
result = await conn.execute("SELECT * FROM products WHERE price < ?;", (50.0,))
rows = await result.fetchall()
```

#### `await conn.executemany(sql: str, parameters_list: Sequence[Sequence[object]]) -> None`
Asynchronously runs high-throughput batch inserts.
- `sql` (`str`): SQL query with placeholders.
- `parameters_list` (`Sequence[Sequence[object]]`): Sequence of parameter tuples/lists.

```python
items = [(1, "Mouse", 25.0), (2, "Keyboard", 75.0)]
await conn.executemany("INSERT INTO products VALUES (?, ?, ?);", items)
```

#### `await conn.prepare(sql: str) -> AsyncPreparedStatement`
Asynchronously compiles a prepared statement.
- `sql` (`str`): Parameterized SQL query.
- **Returns**: `AsyncPreparedStatement`.

```python
stmt = await conn.prepare("INSERT INTO telemetry (device, temp) VALUES (?, ?);")
await stmt.execute("sensor-1", 24.5)
```

#### `await conn.pop(table: str, count: int = 1, direction: str = "fifo") -> list[tuple]`
Asynchronously pops rows from a table as an atomic queue or stack operation directly within the C++ engine.
- `table` (`str`): Table name.
- `count` (`int`, default `1`): Number of rows to remove and return.
- `direction` (`str`, default `"fifo"`):
  - `"fifo"`: Pops from table head (FIFO queue).
  - `"lifo"`: Pops from table tail (LIFO stack).
- **Returns**: List of popped row tuples, each formatted as `(row_id, value)`.

```python
# Pop oldest 10 tasks
tasks = await conn.pop("task_queue", count=10, direction="fifo")
for row_id, payload in tasks:
    print(f"Task {row_id}: {payload}")
```

#### `await conn.count(table: str) -> int`
Asynchronously fetches the total row count of a table.

```python
total = await conn.count("users")
```

#### `await conn.truncate(table: str) -> int`
Asynchronously removes all rows from a table.

```python
deleted_rows = await conn.truncate("temp_session_cache")
```

#### `conn.transaction() -> AsyncTransaction`
Returns an asynchronous ACID transaction context manager for use with `async with`.
- Automatically commits if the context block completes without errors.
- Automatically rolls back if an unhandled exception is raised.

```python
async with conn.transaction():
    await conn.execute("UPDATE wallets SET coins = coins - 10 WHERE user_id = ?;", (1,))
    await conn.execute("UPDATE wallets SET coins = coins + 10 WHERE user_id = ?;", (2,))
```

#### `await conn.begin_transaction() -> AsyncTransaction`
Explicitly begins an asynchronous transaction and returns the active `AsyncTransaction` handle.

```python
txn = await conn.begin_transaction()
try:
    await conn.execute("INSERT INTO audit_log VALUES (?);", ("Event initiated",))
    await conn.commit()
except Exception:
    await conn.rollback()
    raise
```

#### `await conn.commit() -> None`
Asynchronously commits the active transaction on this connection.

#### `await conn.rollback() -> None`
Asynchronously rolls back the active transaction on this connection.

#### `conn.set_autocommit(enabled: bool) -> None`
Toggles autocommit mode on the asynchronous connection.

#### `conn.cancel() -> None`
Sends an immediate cancellation request to terminate an in-flight asynchronous query.

#### `await conn.close() -> None`
Closes the asynchronous connection and releases resources.

---

## `AsyncResult`

Represents the asynchronous cursor returned by `conn.execute()`. Supports both async fetch methods and async iteration (`async for`).

### Methods

#### `await result.fetchone() -> Optional[tuple]`
Fetches the next row asynchronously, or `None` if exhausted.

```python
row = await result.fetchone()
```

#### `await result.fetchmany(size: int) -> list[tuple]`
Fetches up to `size` rows asynchronously.

```python
batch = await result.fetchmany(25)
```

#### `await result.fetchall() -> list[tuple]`
Fetches all remaining rows asynchronously.

```python
all_records = await result.fetchall()
```

#### `async for row in result:`
Streams rows asynchronously through an `async for` loop:

```python
result = await conn.execute("SELECT id, email FROM subscribers;")
async for row in result:
    print("Subscriber:", row[0], row[1])
```

### Properties

- `result.affected_rows` (`int`): Count of rows modified by `INSERT`, `UPDATE`, or `DELETE`.
- `result.has_rows` (`bool`): `True` if result has pending rows.
- `result.columns` (`list`): List of column descriptors and names.
- `result.statistics` (`dict`): Execution duration in microseconds and scan metrics.

---

## `AsyncPreparedStatement`

A pre-compiled SQL statement for asynchronous reuse.

### Methods

#### `await stmt.execute(*parameters: object) -> AsyncResult`
Executes the prepared statement asynchronously with positional arguments.

```python
stmt = await conn.prepare("SELECT * FROM orders WHERE status = ? AND total > ?;")
res1 = await stmt.execute("completed", 100.0)
res2 = await stmt.execute("shipped", 50.0)
```

---

## `AsyncConnectionPool`

Connection pool for managing asynchronous connections across concurrent coroutines.

### Methods

#### `pool.acquire() -> AsyncConnection`
Acquires an available connection from the pool. Supports both direct `await` and `async with` context manager syntax:

```python
# Idiomatic async with context manager (recommended):
async with pool.acquire() as conn:
    await conn.execute("INSERT INTO telemetry VALUES (?);", (98.6,))

# Or explicit acquire and release:
conn = await pool.acquire()
try:
    await conn.execute("SELECT 1;")
finally:
    await conn.close()
```

#### `pool.connection()`
Alias for `pool.acquire()`.

#### `await pool.close() -> None`
Closes the connection pool and releases pool leases.

#### Full Async Pool Example

```python
import asyncio
import nemo

async def handle_request(pool, req_id: int):
    async with pool.acquire() as conn:
        await conn.execute("INSERT INTO requests VALUES (?, ?);", (req_id, "processed"))

async def main():
    with nemo.connect("service") as db:
        pool = db.create_pool_async(max_connections=16)
        
        # Concurrently process 100 requests across pool
        await asyncio.gather(*[handle_request(pool, i) for i in range(100)])
        
        await pool.close()

asyncio.run(main())
```

---

## Related Runnable Examples

- [`example_2.py`](../example/example_2.py): Asynchronous Operations and Async Connection Pools (`AsyncConnectionPool`).
- [`example_40.py`](../example/example_40.py): End-to-End Async Microservice Architecture.


