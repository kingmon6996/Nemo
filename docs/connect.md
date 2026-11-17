# Connecting to Nemo

This guide explains how to connect to and configure Nemo databases using the Python SDK.

---

## `nemo.connect()`

The only entry point for opening a Nemo database.

```python
import nemo

db = nemo.connect(name: str, buffer_pool_mb: int | None = None)
```

### Parameters

| Parameter | Type | Required | Description |
|:---|:---|:---|:---|
| `name` | `str` | ✅ Yes | Plain database name. No extension, no path, no dots. |
| `buffer_pool_mb` | `int \| None` | No | RAM limit for the buffer pool in MB. Defaults to 1/4 of available system RAM (auto-detected), clamped 32 MB – 8 GB. |

### Storage Location

Nemo **always** stores databases in `.nemo/<name>/` relative to the current working directory. You cannot and must not pass a custom path.

```
<cwd>/
└── .nemo/
    └── <name>/
        ├── data.nm      ← main data file (8 KB pages, 128-byte DB header)
        ├── wal/         ← WAL segment files (Phase 3+)
        └── lock         ← process-level lock file (Phase 5+)
```

Nemo creates these directories and files automatically on first connect. If they already exist, Nemo opens the existing database.

### Name Rules

| Rule | Example (valid) | Example (invalid) |
|:---|:---|:---|
| Plain alphanumeric + underscore | `mydb`, `users_v2` | — |
| No dots or file extensions | — | `mydb.nm`, `db.v2` |
| No path separators | — | `data/mydb`, `data\mydb` |
| No `..` traversal | — | `../etc` |
| No `:memory:` | — | `:memory:` |
| Non-empty string | — | `""` |

Violations raise `ValueError` with a descriptive message.

---

## Examples

### Basic connect

```python
import nemo

# Creates or opens .nemo/mydb/ in the current working directory
db = nemo.connect("mydb")
conn = db.connect_sync()
conn.execute("CREATE TABLE t (id INT, v TEXT);")
db.close()
```

### With buffer pool limit

```python
import nemo

# Pin the buffer pool to 512 MB
db = nemo.connect("analytics", buffer_pool_mb=512)
```

### Context manager

```python
import nemo

with nemo.connect("production") as db:
    with db.connect_sync() as conn:
        result = conn.execute("SELECT * FROM accounts;")
        for row in result:
            print(row)
```

### Async usage

```python
import asyncio, nemo

async def main():
    with nemo.connect("production") as db:
        async with db.connect_async() as conn:
            result = await conn.execute("SELECT count(*) FROM events;")
            print(await result.fetchone())

asyncio.run(main())
```

---

## `nemo.Database` Object

| Attribute | Type | Description |
|:---|:---|:---|
| `db.name` | `str` | Plain database name passed to `connect()`. |
| `db.path` | `str` | Absolute path to the `.nemo/<name>/` root folder. |
| `db.is_memory` | `bool` | Always `False` — Nemo is disk-primary only. |

### Connector Methods

| Method | Returns | Description |
|:---|:---|:---|
| `db.connect()` | `SyncConnection` | Synchronous connection (alias for `connect_sync`). |
| `db.connect_sync()` | `SyncConnection` | Synchronous connection. |
| `db.connect_async()` | `AsyncConnection` | Asyncio connection. |
| `db.create_pool_sync(max_connections=16)` | `SyncConnectionPool` | Thread-safe synchronous pool. |
| `db.create_pool_async(max_connections=16)` | `AsyncConnectionPool` | Asyncio-compatible pool. |
| `db.flush()` | `None` | Flush dirty buffer-pool pages to disk. |
| `db.checkpoint()` | `None` | Checkpoint WAL to main data file. |
| `db.close()` | `None` | Close database handle and release resources. |

---

## Connection Pooling

```python
import nemo

with nemo.connect("production") as db:
    pool = db.create_pool_sync(max_connections=8)
    with pool.acquire() as conn:
        conn.execute("INSERT INTO logs VALUES (?);", ("started",))
    pool.close()
```

---

## What Does NOT Exist

- ❌ `path=` parameter — all databases live in `.nemo/<name>/`, period.
- ❌ `timeout=` parameter — connection is always immediate (embedded, local).
- ❌ `:memory:` — Nemo is a strictly disk-primary database engine.
- ❌ Connecting via a `.nm` file path — use the plain name only.

---

## Related

- [HOW_IT_WORKS.md](../HOW_IT_WORKS.md) — Architecture deep-dive.
- [docs/syncconnector.md](syncconnector.md) — Synchronous connection reference.
- [docs/asyncconnector.md](asyncconnector.md) — Async connection reference.
- [docs/sql.md](sql.md) — SQL syntax and DDL reference.
