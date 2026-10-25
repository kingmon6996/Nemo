# Connecting to Nemo Database

This guide explains how to connect to, configure, and manage Nemo databases using the Python SDK.

---

## The `nemo.connect()` Function

The primary entry point for connecting to a Nemo database is `nemo.connect()`.

```python
import nemo

# Syntax:
db = nemo.connect(name: str, path: Optional[str] = None, timeout: float = 0.0)
```

### Parameters

- `name` (`str`, required): The logical name of the database (e.g., `"production"`, `"analytics"`, `"users"`).
  - Only the database name is accepted.
  - Do **not** include file extensions. Nemo strictly manages persistent `.nm` database files on disk.
- `path` (`Optional[str]`, optional): The directory where the database file should reside. Defaults to the current working directory. Parent directories are created automatically if they do not exist.
- `timeout` (`float`, default `0.0`): Connection timeout in seconds. `0.0` disables connection timeout.

### Persistent Local Production Only

Nemo is designed strictly as a high-concurrency, embedded on-disk production engine. In-memory databases (`:memory:`) are not supported; attempting to connect with `:memory:` will raise a `ValueError`.

### Return Value

Returns an instance of `nemo.Database`.

---

## Connection Examples

### 1. Connecting by Database Name

```python
import nemo

# Connects to or creates ./analytics.nm in current directory
db = nemo.connect("analytics")
```

### 2. Connecting with a Target Directory and Timeout

```python
import nemo

# Connects to or creates ./data/storage/production.nm with a 5-second timeout
# Automatically creates the 'data/storage' directory if needed
db = nemo.connect("production", path="data/storage", timeout=5.0)
```

---

## The `nemo.Database` Object

The `Database` instance manages the database handle, path resolution, and connector creation.

### Attributes

- `db.name` (`str`): Base name of the database.
- `db.path` (`str`): Absolute filesystem path to the persistent `.nm` file.
- `db.timeout` (`float`): Configured connection timeout in seconds.

### Connector Methods

| Method | Return Type | Description |
| :--- | :--- | :--- |
| `db.connect()` | `SyncConnection` | Creates a synchronous database connection (alias for `connect_sync`). |
| `db.connect_sync()` | `SyncConnection` | Creates a synchronous database connection. |
| `db.sync_connector()` | `SyncConnector` | Creates a synchronous database connection (alias for `connect_sync`). |
| `db.connect_async()` | `AsyncConnection` | Creates an asynchronous (`asyncio`) database connection. |
| `db.async_connector()` | `AsyncConnector` | Creates an asynchronous database connection (alias for `connect_async`). |
| `db.create_pool_sync(max_connections=16)` | `SyncConnectionPool` | Creates a thread-safe synchronous connection pool. |
| `db.sync_connector_pool(max_connections=16)` | `SyncConnectorPool` | Alias for `create_pool_sync`. |
| `db.create_pool_async(max_connections=16)` | `AsyncConnectionPool` | Creates an asyncio-compatible connection pool. |
| `db.async_connector_pool(max_connections=16)` | `AsyncConnectorPool` | Alias for `create_pool_async`. |
| `db.close()` | `None` | Closes the underlying database handle and releases resources. |

---

## Context Manager Usage

The `Database` object supports Python's `with` statement for automatic closing and resource cleanup.

### Synchronous Context Manager

```python
import nemo

with nemo.connect("analytics", path="data") as db:
    with db.connect_sync() as conn:
        conn.execute("CREATE TABLE metrics (id INTEGER PRIMARY KEY, value DOUBLE);")
        conn.execute("INSERT INTO metrics VALUES (?, ?);", (1, 99.4))
        result = conn.execute("SELECT * FROM metrics;")
        print(result.fetchall())
```

### Asynchronous Context Manager

```python
import asyncio
import nemo

async def main():
    with nemo.connect("analytics", path="data") as db:
        async with db.connect_async() as conn:
            await conn.execute("CREATE TABLE metrics (id INTEGER PRIMARY KEY, value DOUBLE);")
            await conn.execute("INSERT INTO metrics VALUES (?, ?);", (1, 99.4))
            result = await conn.execute("SELECT * FROM metrics;")
            print(await result.fetchall())

asyncio.run(main())
```

---

## Connection Pooling

For multi-threaded or high-concurrency workloads, use connection pools to reuse connection leases.

### Synchronous Pool

```python
import nemo

with nemo.connect("production") as db:
    pool = db.create_pool_sync(max_connections=8)
    
    with pool.acquire() as conn:
        conn.execute("INSERT INTO logs VALUES (?);", ("Application started",))
    
    pool.close()
```

### Asynchronous Pool

```python
import asyncio
import nemo

async def worker(pool, worker_id: int):
    async with pool.connection() as conn:
        await conn.execute("INSERT INTO worker_events VALUES (?, ?);", (worker_id, "active"))

async def main():
    with nemo.connect("production") as db:
        pool = db.create_pool_async(max_connections=8)
        tasks = [worker(pool, i) for i in range(10)]
        await asyncio.gather(*tasks)
        await pool.close()

asyncio.run(main())
```

---

## Installation

Nemo is distributed on PyPI under the official package name `nemo-py`:

```bash
pip install nemo-py
```

Import in Python as:
```python
import nemo
```

---

## Related Runnable Examples

- [`example_1.py`](../example/example_1.py): Basic Synchronous CRUD Operations and Connection Management.
- [`example_2.py`](../example/example_2.py): Asynchronous Operations and `AsyncConnectionPool`.
- [`example_10.py`](../example/example_10.py): Multi-Threaded Sync Connection Pooling and Concurrency.
- [`example_40.py`](../example/example_40.py): End-to-End Microservice Architecture with Connection Pools.


