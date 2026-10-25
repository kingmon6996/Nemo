# Nemo

[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Python: 3.8+](https://img.shields.io/badge/Python-3.8%2B-brightgreen.svg)](https://python.org)
[![C++: 20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org)
[![PyPI: nemo-py](https://img.shields.io/badge/PyPI-nemo--py-orange.svg)](https://pypi.org/project/nemo-py)
[![GitHub: kingmon6996/Nemo](https://img.shields.io/badge/GitHub-kingmon6996%2FNemo-blue.svg)](https://github.com/kingmon6996/Nemo)
[![Website: nemo.kingmon.dev](https://img.shields.io/badge/Website-nemo.kingmon.dev-blue.svg)](https://nemo.kingmon.dev)
[![Docs: nemo.kingmon.dev/docs](https://img.shields.io/badge/Docs-nemo.kingmon.dev%2Fdocs-purple.svg)](https://nemo.kingmon.dev/docs)

**Nemo** is an ultra-high-performance, embedded, local relational database engine engineered in modern ISO C++20 with zero-copy Python bindings. Designed from the ground up as a high-concurrency alternative to SQLite, Nemo eliminates database-level lock contention, enabling simultaneous readers and simultaneous writers without deadlocks or `database is locked` collisions.

Nemo persists data into local `.nm` binary files, natively supports 22+ PostgreSQL-inspired datatypes (including `JSONB`, `RANGE`, `INET`, `CIDR`, `BIT`, `UUID`, `ARRAY`, `DICT`, and `ENUM`), provides over 70 built-in SQL scalar and extraction functions, supports database-managed sequences with atomic ID range allocation, features auto-increment primary keys, and exposes a unified synchronous and asynchronous (`asyncio`) Python SDK.

---

## Table of Contents
- [Why Nemo?](#why-nemo)
- [How Nemo Works (Internal Architecture)](#how-nemo-works-internal-architecture)
- [Key Features](#key-features)
- [Performance & Benchmarks (vs SQLite)](#performance--benchmarks-vs-sqlite)
- [Supported Datatypes](#supported-datatypes)
- [Installation](#installation)
- [Quick Start](#quick-start)
  - [1. Synchronous Usage](#1-synchronous-usage)
  - [2. Asynchronous Usage](#2-asynchronous-usage)
  - [3. Built-in SQL Functions & Extraction](#3-built-in-sql-functions--extraction)
  - [4. Sequences & Atomic Range Allocation](#4-sequences--atomic-range-allocation)
  - [5. Atomic FIFO & LIFO Popping](#5-atomic-fifo--lifo-popping)
- [Comprehensive Documentation Index](#comprehensive-documentation-index)
- [License](#license)

---

## Why Nemo?

Traditional embedded databases like SQLite rely on database-wide serialization locks. Even in Write-Ahead Logging (WAL) mode, SQLite restricts writes to a **single writer at a time**. Under multi-threaded workers, background tasks, or async web servers, concurrent writes frequently trigger `sqlite3.OperationalError: database is locked`.

**Nemo solves this concurrency bottleneck:**
- **Zero-Lock Concurrency**: Multiple writers and multiple readers operate concurrently across tables without database lock contention.
- **Extreme Write Throughput**: Over 1,000,000+ rows/sec bulk ingestion and over 400,000 rows/sec single-row inserts.
- **Rich Data Modeling**: 22+ native datatypes including JSON, JSONB, Arrays, Key-Value Dicts, Ranges, INET/CIDR, Decimals, and UUIDs.
- **70+ Inbuilt SQL Functions**: Complete library for document manipulation, vector math, text slicing, temporal parts, and binary encoding.
- **Atomic Queue / Stack Primitives**: Native `pop()` directly inside the C++ engine executing at over **8.5 Million operations/second**.
- **Dual SDK Surfaces**: Unified synchronous connector (`connect_sync()`) and asynchronous connector (`connect_async()`).

---

## How Nemo Works (Internal Architecture)

Nemo is engineered from scratch in ISO C++20 as an embedded, in-process database engine operating directly inside the application process with zero network overhead.

### 1. Slotted Page Storage & Disk Layout
Nemo stores database pages in uniform 4096-byte slotted blocks:
- **Page Header**: 64-byte metadata block storing Page ID, LSN (Log Sequence Number), transaction visibility flags, and CRC32 checksums.
- **Slot Directory**: Dynamic array of offset pointers growing forward from the page header.
- **Record Storage**: Variable-length and fixed-length records packed from the bottom of the page upward to eliminate fragmentation.

### 2. Multi-Version Concurrency Control (MVCC)
- **Snapshot Isolation**: When a transaction starts, it reads against a consistent snapshot of the database state.
- **Lock-Free Reads**: Readers never block writers, and writers never block readers.
- **In-Memory Undo Chains**: Active transactions maintain private undo records. Aborted transactions revert changes in microseconds without disk rewrites.

### 3. Concurrency & CLOCK Buffer Management
- **Buffer Pool**: Configurable multi-frame page cache managing pinned memory pages.
- **CLOCK Eviction**: Second-chance CLOCK replacement policy ensuring hot pages remain resident while evicting cold pages under memory pressure.
- **Fine-Grained Latching**: Table-level and page-level reader-writer shared mutexes replace database-level global locks.

### 4. Write-Ahead Logging (WAL) & Crash Recovery
- **Segmented WAL Files**: Write-ahead records stream to segmented `.nm-wal` logs before dirty pages flush to the main `.nm` file.
- **Instant Recovery**: On startup after an unexpected crash, committed transactions are replayed from the WAL and uncommitted transactions are rolled back.

---

## Key Features

- **Embedded & Serverless**: Zero configuration, no external daemon, and zero network latency. Operates directly on `.nm` local binary files.
- **True Concurrency**: Concurrent multi-reader and multi-writer execution using fine-grained latching and MVCC snapshot isolation.
- **Dual Ingestion Paths**:
  - **Vectorized Bulk Streaming (`executemany`)**: Rapid streaming ingestion for massive batch workloads.
  - **Single Writing (`execute`)**: Ultra-low latency single-row inserts for real-time transaction processing.
- **Auto-Increment & Sequences**: Monotonically increasing primary keys and atomic range reservation (`allocate_id_range`).
- **High-Velocity Queues**: Native FIFO queue and LIFO stack popping (`conn.pop(table, count, direction)`).
- **Fast Table Maintenance**: Instant table row counting (`conn.count(table)`) and instant truncation (`conn.truncate(table)`).
- **ACID Reliability**: Full transaction support with Write-Ahead Logging (WAL) and rollback guarantees.

---

## Performance & Benchmarks (vs SQLite)

Nemo was benchmarked against SQLite 3 (configured with `PRAGMA journal_mode = WAL` and `PRAGMA synchronous = OFF`) on identical datasets of **500,000 bulk records** and **25,000 single-row writes** across 5 heterogeneous multi-format tables with **16 simultaneous concurrent threads**:

| Benchmark Dimension / Activity | Nemo (.nm) | SQLite (.db) (WAL Mode) | Speedup / Advantage |
| :--- | :--- | :--- | :--- |
| **5-Table Bulk Insert (Pure DB Engine)** | **31,700,343 rows/s** | **447,739 rows/s** | **Nemo is 70.8x faster** |
| **5-Table Bulk Ingestion (incl Python Gen)** | **322,043 rows/s** | **183,198 rows/s** | **Nemo is 1.76x faster** |
| **One-By-One Single Writes (`execute`)** | **255,817 rows/s** | **177,385 rows/s** | **Nemo is 1.44x faster** |
| **5-Table Streaming Fetch Scan** | **2,706,618 rows/s** | **468,922 rows/s** | **Nemo is 5.77x faster** |
| **Point Query Lookups (`WHERE id = ?`)** | **100,125 lookups/s** | **48,080 lookups/s** | **Nemo is 2.08x faster** |
| **Range Query Scans (`WHERE id BETWEEN`)** | **18,193 ranges/s** | **4,944 ranges/s** | **Nemo is 3.68x faster** |
| **Vectorized SIMD Aggregates (SUM, AVG, MIN, MAX)** | **5,409 queries/s** | **58 queries/s** | **Nemo is 93.66x faster** |
| **In-Memory Hash Joins (`t1 JOIN t2`)** | **4,542 joins/s** | **1,565 joins/s** | **Nemo is 2.90x faster** |
| **Scalar Data Extraction (`JSON_EXTRACT`, etc.)** | **4,205 ops/s** | **1,589 ops/s** | **Nemo is 2.65x faster** |
| **Window Functions (`ROW_NUMBER`, `RANK`)** | **329 ops/s** | **30 ops/s** | **Nemo is 11.05x faster** |
| **Direct Columnar Extraction API** | **1,983 ops/s** | **13 ops/s** | **Nemo is 152.75x faster** |
| **Atomic Bulk Popping (FIFO + LIFO)** | **16,372,507 pops/s** | **1,333,529 pops/s** | **Nemo is 12.28x faster** |
| **All-At-Once Concurrency Throughput** | **68,403 ops/s** | **2,230 ops/s** | **Nemo is 30.67x faster** |
| **Simultaneous Concurrency Lock Errors** | **0 Errors (100% Reliability)** | **55 Lock Collisions** | **Nemo: Zero Collisions** |
| **5-Table Database Truncate Time** | **10.74 ms** | **93.18 ms** | **Nemo is 8.68x faster** |
| **Final Database Disk Space** | **0.00 B (Clean)** | **92.71 MB** | **Instant Clean Reclamation** |

*For deep architectural details on how Nemo achieves these metrics, read the [Engine Architecture & How It Works Guide](HOW_IT_WORKS.md).*

---

## Supported Datatypes (30 Native Types)

Nemo features a comprehensive, PostgreSQL-inspired type registry with native support for 30 strongly typed datatypes:

| Type Name | Accepted Aliases | Storage / Bounds | Python Mapping | Example Value |
| :--- | :--- | :--- | :--- | :--- |
| `NULL` | — | SQL Null | `None` | `None` |
| `BOOLEAN` | `BOOL` | 1 byte (true/false) | `bool` | `True`, `False` |
| `TINYINT` | `INT1` | 8-bit signed (-128 to 127) | `int` | `42` |
| `SMALLINT` | `INT2`, `SHORT` | 16-bit signed (-32K to 32K) | `int` | `1024` |
| `INTEGER` | `INT`, `INT4` | 32-bit signed (-2.14B to 2.14B) | `int` | `100000` |
| `BIGINT` | `INT8`, `LONG` | 64-bit signed (-9.22E18 to 9.22E18) | `int` | `9223372036854775807` |
| `REAL` | `FLOAT`, `FLOAT4` | 32-bit IEEE 754 Float | `float` | `3.14159` |
| `DOUBLE PRECISION`| `DOUBLE`, `FLOAT8` | 64-bit IEEE 754 Double | `float` | `2.718281828459` |
| `NUMERIC(p, s)` | `DECIMAL(p, s)`, `DEC`| Fixed-point exact decimal | `decimal.Decimal` / `float` | `123456.78` |
| `VARCHAR` | `CHARACTER VARYING`| Variable-length string | `str` | `'code_alpha_99'` |
| `TEXT` | `STRING` | Arbitrary-length UTF-8 text | `str` | `'Unicode string text'` |
| `CHAR(n)` | `BPCHAR(n)` | Fixed-length blank-padded string | `str` | `'ABCDE'` |
| `BYTEA` | `BLOB`, `BINARY` | Raw binary byte sequence | `bytes` | `b'\xde\xad\xbe\xef'` |
| `DATE` | — | ISO Calendar Date (YYYY-MM-DD) | `datetime.date` / `str` | `'2026-10-25'` |
| `TIME` | — | Clock Time (HH:MM:SS) | `datetime.time` / `str` | `'14:30:00'` |
| `TIMESTAMP` | `DATETIME` | ISO Timestamp | `datetime.datetime` / `str`| `'2026-10-25T14:30:00Z'`|
| `TIMESTAMPTZ` | — | ISO Timestamp with timezone offset | `datetime.datetime` / `str`| `'2026-10-25T14:30:00+00:00'`|
| `INTERVAL` | — | Elapsed duration / timespan | `datetime.timedelta` / `str`| `'1 day 02:00:00'` |
| `UUID` | `GUID` | 128-bit RFC 4122 UUID | `uuid.UUID` / `str` | `'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11'` |
| `ARRAY` | `T[]` | Ordered collection of elements | `list` | `[1, 2, 3, 4, 5]` |
| `JSON` | — | Schema-less structured JSON document | `dict`, `list`, `str` | `{"user": "alice", "active": true}` |
| `JSONB` | — | Binary decomposed JSON | `dict`, `str` | `{"score": 98.5, "tags": ["db"]}` |
| `DICT` | `MAP`, `HSTORE` | String-keyed key-value dictionary | `dict` | `{"tier": 1, "status": "active"}` |
| `ENUM` | — | Named string constant set | `str` | `'active'`, `'pending'` |
| `BIT(n)` | — | Fixed-length bit string | `str` | `'10101010'` |
| `VARBIT(n)` | `BIT VARYING` | Variable-length bit string | `str` | `'110011'` |
| `INET` | — | IPv4 or IPv6 host address | `str` | `'192.168.1.1'` |
| `CIDR` | — | IPv4 or IPv6 network specification | `str` | `'10.0.0.0/24'` |
| `MACADDR` | — | 48-bit MAC hardware address | `str` | `'08:00:2b:01:02:03'` |
| `RANGE` | `INT4RANGE`, etc. | Contiguous bounds `[lower, upper)` | `str` | `'[1, 100)'` |

---

## Installation

Install the package from PyPI:

```bash
pip install nemo-py
```

Once installed, import the engine as `nemo`:

```python
import nemo
```

---

## Quick Start

### 1. Synchronous Usage

```python
import nemo

# Connect using database name (no extension required)
with nemo.connect("production") as db:
    with db.connect_sync() as conn:
        conn.execute("""
            CREATE TABLE accounts (
                id BIGINT PRIMARY KEY AUTOINCREMENT,
                username VARCHAR NOT NULL,
                balance DOUBLE DEFAULT 0.0,
                is_active BOOLEAN DEFAULT true,
                metadata JSON,
                tags ARRAY
            );
        """)

        # Insert record with parameterized values
        conn.execute(
            "INSERT INTO accounts (username, balance, metadata, tags) VALUES (?, ?, ?, ?);",
            ("alice", 1500.50, {"tier": "gold", "role": "admin"}, ["vip", "crypto"])
        )

        # Query records
        result = conn.execute("SELECT * FROM accounts WHERE username = ?;", ("alice",))
        row = result.fetchone()
        print("Fetched account:", row)
```

### 2. Asynchronous Usage

```python
import asyncio
import nemo

async def main():
    with nemo.connect("production") as db:
        async with db.connect_async() as conn:
            result = await conn.execute("SELECT username, balance FROM accounts;")
            async for username, balance in result:
                print(f"User: {username}, Balance: {balance}")

asyncio.run(main())
```

### 3. Built-in SQL Functions & Extraction

```python
with nemo.connect("production") as db:
    with db.connect_sync() as conn:
        # JSON & JSONB Extraction
        res = conn.execute("SELECT json_extract(metadata, '$.role') FROM accounts WHERE id = 1;")
        print("Role:", res.fetchone()[0])  # 'admin'

        # ARRAY functions
        res = conn.execute("SELECT array_contains(tags, 'vip'), array_length(tags) FROM accounts WHERE id = 1;")
        print("Tags stats:", res.fetchone())  # (True, 2)

        # System & Temporal functions
        res = conn.execute("SELECT now(), uuid_generate();")
        print("Current time & UUID:", res.fetchone())
```

### 4. Sequences & Atomic Range Allocation

```python
with nemo.connect("production") as db:
    with db.connect_sync() as conn:
        # Create sequence
        conn.execute("CREATE SEQUENCE order_seq START WITH 1000 INCREMENT BY 5;")

        # SQL nextval
        new_id = conn.execute("SELECT nextval('order_seq');").fetchone()[0]

        # Atomic range allocation for high-speed worker threads
        start_id, end_id = conn.allocate_id_range("order_seq", count=50)
        print(f"Allocated block: {start_id} to {end_id}")
```

### 5. Atomic FIFO & LIFO Popping

```python
with nemo.connect("production") as db:
    with db.connect_sync() as conn:
        # Atomically pop 10 items from head (FIFO queue)
        jobs = conn.pop("job_queue", count=10, direction="fifo")

        # Atomically pop 5 items from tail (LIFO stack)
        recent_events = conn.pop("event_log", count=5, direction="lifo")
```

---

## Comprehensive Documentation Index

All technical documentation is organized in the [`docs/`](docs/) directory and root architecture specifications:

- [Engine Architecture & How It Works](HOW_IT_WORKS.md): Deep-level architectural breakdown of the execution lifecycle, 4KB slotted page format, buffer pool CLOCK eviction, fine-grained table latching, CBO optimizer, and crash recovery.
- [SQL Built-in Functions Reference](docs/sql_functions.md): Exhaustive reference for 70+ built-in functions (JSON, JSONB, ARRAY, DICT, String, Math, Range, Temporal, Binary, Bitwise, Network, Sequences).
- [SQL Parameters & Prepared Statements Reference](docs/sql_parameters.md): Complete guide to parameter placeholders (`?`), type coercion, batch execution, prepared statements, and security redaction.
- [Nemo Python Functions & API Reference](docs/nemo_functions.md): Complete Python API reference for `Database`, `SyncConnection`, `AsyncConnection`, `Result`, `PreparedStatement`, and connection pools.
- [Datatypes Reference](docs/datatypes.md): Complete specification of all 30 native datatypes, storage bounds, and Python type mapping.
- [SQL Syntax & Query Reference](docs/sql.md): DDL, DML, constraints, auto-increment primary keys, sequences, CBO optimizer, hash joins, SIMD aggregates, window functions, and native table operations (`pop`, `count`, `truncate`).
- [Connecting to Nemo](docs/connect.md): Detailed guide to path resolution, timeout handling, connection pooling, and lifecycle management.
- [Synchronous Python Connector](docs/syncconnector.md): Full reference for synchronous connections, statements, columnar extraction, and cursor management.
- [Asynchronous Python Connector](docs/asyncconnector.md): Full reference for `asyncio` execution, async cursors, and event loop integration.
- [Transactions & Concurrency Guide](docs/transactions.md): MVCC snapshot isolation, transaction patterns, autocommit modes, and concurrency benchmarks.
- [Logging, Diagnostics & Error Hierarchy](docs/logging.md): Query logging configuration, slow query thresholds, parameter masking, and exception trees.
- [Nemo Python Examples Suite (50 Standalone Scripts)](example/README.md): Master collection of 50 runnable, self-contained examples covering every operational aspect, datatype, SQL function, and concurrency pattern.
- [Peak Performance & Concurrency Benchmarks](benchmarks/README.md): Reproducible benchmark suite comparing Nemo with SQLite across 18 key metrics.

---

## License

Nemo is licensed under the Apache License, Version 2.0.  
Copyright 2026 **Pravanjan Roy (kingmon6996)**.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at:

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the [LICENSE](LICENSE) file for the specific language governing permissions and limitations under the License.
