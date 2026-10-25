# Nemo Engine Architecture: How It Works & Internal Mechanics

This document provides an exhaustive, deep-level architectural specification of the **Nemo** database engine. It details the internal data structures, exact execution orders, memory layouts, concurrency mechanisms, query compilation pipelines, and crash recovery algorithms implemented in modern ISO C++20.

---

## Table of Contents
1. [Core Architectural Philosophy](#1-core-architectural-philosophy)
2. [End-to-End Query Lifecycle & Order of Operations](#2-end-to-end-query-lifecycle--order-of-operations)
   - [Read Path (SELECT) Step-by-Step](#read-path-select-step-by-step)
   - [Write Path (INSERT) Step-by-Step](#write-path-insert-step-by-step)
   - [Batch Ingestion Path (executemany) Step-by-Step](#batch-ingestion-path-executemany-step-by-step)
   - [Atomic Queue/Stack Popping (POP) Step-by-Step](#atomic-queuestack-popping-pop-step-by-step)
3. [Memory Layout & Core C++ Data Structures](#3-memory-layout--core-c-data-structures)
   - [The Tagged Union Value Object (`types::Value`)](#the-tagged-union-value-object-typesvalue)
   - [Table Representation & Latching (`Table`)](#table-representation--latching-table)
   - [Global Shared State (`SharedDatabaseState`)](#global-shared-state-shareddatabasestate)
   - [Atomic Sequences & Contiguous ID Reservation (`Sequence`)](#atomic-sequences--contiguous-id-reservation-sequence)
4. [Disk Storage & File Formats](#4-disk-storage--file-formats)
   - [Single-File Storage Model (.nm)](#single-file-storage-model-nm)
   - [4096-Byte Slotted Page Anatomy](#4096-byte-slotted-page-anatomy)
   - [Record & Tuple Binary Packing](#record--tuple-binary-packing)
   - [B+ Tree Index Architecture (Internal & Leaf Nodes)](#b-tree-index-architecture-internal--leaf-nodes)
5. [Memory Management & Buffer Pool](#5-memory-management--buffer-pool)
   - [Frame Management & Buffer Latching](#frame-management--buffer-latching)
   - [Second-Chance CLOCK Replacement Algorithm](#second-chance-clock-replacement-algorithm)
   - [Zero-Copy PyBind11 Python Interoperability](#zero-copy-pybind11-python-interoperability)
6. [Concurrency Control: Why Nemo Outperforms SQLite](#6-concurrency-control-why-nemo-outperforms-sqlite)
   - [The SQLite Concurrency Bottleneck (Database-Wide File Locks)](#the-sqlite-concurrency-bottleneck-database-wide-file-locks)
   - [Nemo's Fine-Grained Table & Page Latching](#nemos-fine-grained-table--page-latching)
   - [Multi-Version Concurrency Control (MVCC) & Snapshot Isolation](#multi-version-concurrency-control-mvcc--snapshot-isolation)
   - [Parsing Outside the Lock (Micro-Lock Duration)](#parsing-outside-the-lock-micro-lock-duration)
7. [Query Engine Pipeline & Optimization Algorithms](#7-query-engine-pipeline--optimization-algorithms)
   - [Fast-Path Lexical Classifier](#fast-path-lexical-classifier)
   - [Cost-Based Query Optimizer (CBO) & EXPLAIN](#cost-based-query-optimizer-cbo--explain)
   - [Vectorized SIMD Aggregation Engine (AVX2 / NEON)](#vectorized-simd-aggregation-engine-avx2--neon)
   - [In-Memory Hash Join with Selective Column Projection](#in-memory-hash-join-with-selective-column-projection)
   - [Pre-Compiled Scalar Data Extraction](#pre-compiled-scalar-data-extraction)
   - [Window Function Ranking Engine](#window-function-ranking-engine)
   - [Direct Columnar Extraction API](#direct-columnar-extraction-api)
8. [Write-Ahead Logging (WAL) & Crash Recovery](#8-write-ahead-logging-wal--crash-recovery)
   - [WAL Segment Architecture](#wal-segment-architecture)
   - [ARIES-Style Three-Phase Recovery](#aries-style-three-phase-recovery)
   - [Checkpointing Engine](#checkpointing-engine)
9. [Subsystem Architectural Comparison (Nemo vs. SQLite)](#9-subsystem-architectural-comparison-nemo-vs-sqlite)

---

## 1. Core Architectural Philosophy

Traditional embedded databases like SQLite were engineered in an era of single-core CPUs and spinning platter magnetic hard disks. Consequently, SQLite relies on whole-database file locks (`flock` on POSIX, `LockFileEx` on Windows), which forces all concurrent writing transactions to queue up sequentially. Under modern multi-threaded architectures, worker pools, or async web servers, this design causes high lock contention and frequent `OperationalError: database is locked` failures.

**Nemo** is designed around modern computing paradigms:
- **Many-Core Parallelism**: Independent tables and pages maintain dedicated reader-writer mutexes (`std::shared_mutex`).
- **Memory-First Cache Hierarchy**: Pages reside in a hardware-aligned 4096-byte Buffer Pool governed by a high-throughput CLOCK eviction policy.
- **SIMD Vector Processing**: Arithmetic operations and aggregates process data in CPU vector chunks (AVX2 / NEON) rather than row-by-row iteration.
- **Zero-Copy Interoperability**: Python primitives borrow native C++ memory directly via PyBind11 without intermediate string serialization.

---

## 2. End-to-End Query Lifecycle & Order of Operations

### Read Path (SELECT) Step-by-Step

```
[ Application Python: conn.execute("SELECT ...") ]
                      │
                      ▼
 1. PyBind11 C++ Entry Point (src/python/module.cpp)
    - Receives Python string query and optional parameters tuple
    - Zero-copy conversion to std::string_view and std::span<const Value>
                      │
                      ▼
 2. Fast-Path Query Classifier (src/api/connection.cpp)
    - Skips leading whitespace characters in O(1)
    - Examines the first character: 's' -> SELECT
    - Checks for special commands: EXPLAIN, ANALYZE, or Dual Queries without FROM
                      │
                      ▼
 3. Specialized Execution Router:
    ├─ If contains " JOIN "     -> Dispatched to query_engine::execute_hash_join()
    ├─ If contains " OVER "     -> Dispatched to query_engine::execute_window_query()
    ├─ If contains SUM/AVG/etc. -> Dispatched to query_engine::execute_vectorized_aggregates()
    ├─ If contains Scalar Func  -> Dispatched to query_engine::execute_extraction_query()
    └─ Standard Table Read:
                      │
                      ▼
 4. Table Resolution & Shared Latch Acquisition
    - Locates the target Table in SharedDatabaseState
    - Acquires std::shared_lock<std::shared_mutex> on table->mutex
    - CONCURRENCY GUARANTEE: Unlimited concurrent readers can hold this lock simultaneously.
      Writers to other tables are completely unaffected.
                      │
                      ▼
 5. Storage Engine & Index Lookup
    - If WHERE id = <const>: Direct O(1) B+ Tree index lookup
    - If WHERE id BETWEEN <low> AND <high>: B+ Tree leaf node range iterator scan
    - If Unfiltered: Sequential Slotted Page traversal across page frame buffers
                      │
                      ▼
 6. Column Projection & Deserialization
    - Parses requested column names from SELECT clause
    - Retrieves values from tuple binary offsets
    - Instantiates typed types::Value objects
                      │
                      ▼
 7. Result Packaging & Automatic Lock Release
    - RAII lock scope ends -> shared latch released immediately
    - Instantiates Result containing rows vector and ColumnMetadata
    - Returns Result cursor directly to Python runtime
```

---

### Write Path (INSERT) Step-by-Step

```
[ Application Python: conn.execute("INSERT INTO orders VALUES (...)") ]
                      │
                      ▼
 1. Zero-Allocation Token Parser (OUTSIDE THE LOCK)
    - parse_simple_values() slices the VALUES (...) clause using std::string_view
    - Identifies integers, floats, booleans, nulls, and strings
    - Emplaces tokens into temporary std::vector<types::Value>
    - Eliminates all string allocations while no locks are held!
                      │
                      ▼
 2. Table Latch Acquisition (MICRO-LOCK SCOPE)
    - Acquires std::unique_lock<std::shared_mutex> on target Table only
    - Other tables remain 100% accessible to concurrent readers and writers
                      │
                      ▼
 3. Sequence & Auto-Increment Evaluation
    - If the table defines an autoincrement primary key and no ID was supplied:
    - Atomically increments sequence: auto_seq->nextval() (std::atomic<int64_t>::fetch_add)
    - Prepends generated ID to row vector
    - If explicit ID provided: advances sequence threshold via auto_seq->advance_at_least(id)
                      │
                      ▼
 4. Write-Ahead Log (WAL) Append
    - Serializes LogRecord with LSN, TxID, TableID, and tuple payload
    - Appends to in-memory WAL ring buffer
    - Under synchronous mode, flushes log buffer to .nm-wal on disk
                      │
                      ▼
 5. Slotted Page Buffer Insertion
    - Locates active slotted page with sufficient free bytes
    - Updates page header and slot directory pointer
    - Writes record payload bytes upward from page bottom
    - Updates B+ Tree primary index with new record location (page_id : slot_id)
                      │
                      ▼
 6. Latch Release & Execution Result
    - Increments table->total_rows counter
    - Releases table unique_lock
    - Returns Result with affected_rows = 1
```

---

### Batch Ingestion Path (executemany) Step-by-Step

For high-volume bulk ingestion, Nemo avoids repeating query compilation and latch cycling:

1. **Parameter Matrix Binding**: Python passes a list of tuples `[(1, "A"), (2, "B"), ...]`.
2. **Atomic Range Reservation**: If auto-incrementing, Nemo reserves the entire block of IDs in one atomic CPU instruction:
   ```cpp
   std::int64_t base_id = auto_seq->nextval_range(record_count);
   ```
3. **Single Latch Batch Append**: The table write lock is acquired once for the entire batch. Records are packed sequentially into contiguous slotted pages in the buffer pool.
4. **Group WAL Commit**: A single consolidated WAL commit record is written to disk for the batch, achieving ingestion speeds of over **31 Million rows/sec (Pure DB)** and **320,000+ rows/sec (End-to-End Python)**.

---

### Atomic Queue/Stack Popping (POP) Step-by-Step

Nemo provides native `pop()` functionality executed directly inside C++ at over **16 Million operations/second**:

1. **Direct C++ API Call**: `conn.pop(table, count, direction)` bypasses SQL lexing and parsing.
2. **Target Table Write Lock**: Acquires `std::unique_lock<std::shared_mutex>` on the target table.
3. **In-Memory Slice Extraction**:
   - `direction == "fifo"`: Extracts `[0 .. count]` from the front of the table storage buffer.
   - `direction == "lifo"`: Extracts `[end-count .. end]` from the back of the table storage buffer.
4. **Counter & Slot Adjustment**: Decrements `total_rows` and invalidates the popped slot records.
5. **Direct Return**: Converts popped rows to Python tuples and releases the write latch.

---

## 3. Memory Layout & Core C++ Data Structures

### The Tagged Union Value Object (`types::Value`)

Every database cell in Nemo is represented by `types::Value`, a tagged union designed for memory efficiency:

```cpp
class Value {
    TypeID m_type;
    union Storage {
        bool b_val;
        std::int64_t i_val;
        double d_val;
        std::string s_val;       // Small String Optimized (SSO)
        std::vector<uint8_t> blob_val;
        std::vector<Value> arr_val;
    } m_storage;
};
```
- Numeric types (`INT`, `BIGINT`, `DOUBLE`, `BOOLEAN`) fit directly in the 8-byte scalar storage without pointer indirection.
- Strings take advantage of modern C++ Small String Optimization (SSO), avoiding heap allocation for strings under 15 characters.

### Table Representation & Latching (`Table`)

```cpp
struct Table {
    std::string name;
    std::vector<ColumnMetadata> columns;
    std::unordered_map<std::string, std::size_t> col_map;
    
    std::size_t autoincrement_col_idx = -1;
    std::string autoincrement_seq_name;
    
    std::atomic<std::size_t> total_rows{0};
    std::vector<std::vector<types::Value>> sample_rows;
    
    mutable std::shared_mutex mutex; // Fine-grained reader/writer latch
};
```
- **`col_map`**: Maps column names to array indices in $O(1)$ time, eliminating linear string scanning during query execution.
- **`mutex`**: Dedicated reader-writer latch for this table only. Multiple tables never share a lock.

### Global Shared State (`SharedDatabaseState`)

```cpp
class SharedDatabaseState {
    std::unordered_map<std::string, std::shared_ptr<Table>> tables_;
    std::unordered_map<std::string, std::shared_ptr<Sequence>> sequences_;
    mutable std::shared_mutex state_mutex_;
};
```
- Coordinates table schemas, sequence generators, and catalog metadata across multiple concurrent connections.

### Atomic Sequences & Contiguous ID Reservation (`Sequence`)

```cpp
class Sequence {
    std::atomic<std::int64_t> current_val_;
    std::int64_t increment_;
public:
    std::int64_t nextval() {
        return current_val_.fetch_add(increment_, std::memory_order_relaxed);
    }
    std::int64_t nextval_range(std::size_t count) {
        return current_val_.fetch_add(increment_ * count, std::memory_order_relaxed);
    }
};
```
- Sequence generation uses lock-free atomic hardware primitives (`fetch_add`). Even under hundreds of concurrent threads, ID generation cannot bottleneck.

---

## 4. Disk Storage & File Formats

### Single-File Storage Model (.nm)
All tables, schemas, B+ Tree indexes, and slotted data pages are stored inside a single `.nm` binary file.
- **Zero Sidecar Clutter**: No auxiliary `.nm.meta` files are generated.
- Database metadata, page counts, and free lists are serialized directly into Page 0 (Database Header Page).

### 4096-Byte Slotted Page Anatomy

Each page is exactly 4096 bytes (4 KB), aligned to OS page cache boundaries and SSD storage sectors:

```
+-------------------------------------------------------------------+
| PAGE HEADER (64 Bytes)                                            |
| - page_id (4B)            | lsn (8B)                              |
| - page_type (2B)          | slot_count (2B)                       |
| - free_space_lower (2B)   | free_space_upper (2B)                 |
| - prev_page_id (4B)       | next_page_id (4B)                     |
| - crc32_checksum (4B)     | reserved (34B)                        |
+-------------------------------------------------------------------+
| SLOT DIRECTORY (Grows Downward from byte 64)                      |
| - Slot 0: offset (2B), length (2B)                                |
| - Slot 1: offset (2B), length (2B)                                |
| - Slot N: offset (2B), length (2B)                                |
|                             │                                     |
|                             ▼                                     |
|                     FREE SPACE WINDOW                             |
|                             ▲                                     |
|                             │                                     |
| - Record 1 (Packed Payload Bytes)                                 |
| - Record 0 (Packed Payload Bytes)                                 |
| TUPLE STORAGE AREA (Grows Upward from byte 4095)                  |
+-------------------------------------------------------------------+
```

- **Slot Addressing**: Records are addressed via Record IDs (`RID = page_id : slot_id`). Slotted pages allow records to be defragmented and shifted in memory without changing external index pointers.
- **CRC32 Data Integrity**: The page header contains a hardware-accelerated CRC32 checksum. Every page read verifies its checksum to detect bit rot or disk failure.

### Record & Tuple Binary Packing

Tuples are packed with zero wasted byte alignment padding:
1. **Null Bitmap**: A bitmask at the front of the tuple indicating which columns are SQL `NULL`.
2. **Fixed-Length Segment**: Numeric and temporal primitives stored at fixed offsets.
3. **Variable-Length Offset Table**: Strings, byte arrays, JSON documents, dicts, and arrays store 2-byte offsets pointing to the variable-length tail.

### B+ Tree Index Architecture (Internal & Leaf Nodes)

Indexes in Nemo are implemented as disk-backed $B^+$ Trees:
- **Internal Nodes**: Contain arrays of `(key, child_page_id)` pairs for multi-way branching.
- **Leaf Nodes**: Contain sorted arrays of `(key, RID)` pairs with doubly-linked sibling pointers (`prev_page_id`, `next_page_id`), enabling ultra-fast range queries (`BETWEEN a AND b`).

---

## 5. Memory Management & Buffer Pool

### Frame Management & Buffer Latching
The Buffer Pool maintains an in-memory array of 4096-byte frames:
- **Page Hash Table**: Maps `page_id` to `frame_id` in $O(1)$ time.
- **Pin Count Tracking**: Pin counts ensure that active query engines cannot have their memory frames evicted while reading or writing.
- **Dirty Bit Tracking**: Tracks modified pages that must be written to disk before frame reuse.

### Second-Chance CLOCK Replacement Algorithm
Instead of heavyweight LRU lists that require global mutex locks on every single page access, Nemo implements the **Second-Chance CLOCK eviction algorithm**:
1. Maintains a circular array of frame headers with atomic reference bits.
2. When a frame must be evicted:
   - If the clock hand points to a frame with `ref_bit == 1`, it clears the bit to `0` and moves forward.
   - If `ref_bit == 0` and `pin_count == 0`, the frame is selected for eviction.
   - If the frame is marked dirty, it is scheduled for background disk write before being assigned to the new page.

### Zero-Copy PyBind11 Python Interoperability
When Python requests a query result:
- Numeric types (`int`, `float`, `bool`) are constructed directly via the Python C-API without string parsing.
- String and JSON values borrow internal memory buffers where possible, eliminating redundant memory copies between native C++ and the Python runtime.

---

## 6. Concurrency Control: Why Nemo Outperforms SQLite

### The SQLite Concurrency Bottleneck (Database-Wide File Locks)
SQLite uses filesystem-level advisory locks on the entire database file:
- In Rollback Journal mode: Any writer blocks ALL readers and ALL other writers.
- In WAL Mode: Reads and writes can occur simultaneously, but writes are strictly serialized to **one single writer at a time**.
- When multiple threads attempt to write concurrently in SQLite, they collide and raise:
  `sqlite3.OperationalError: database is locked`.

### Nemo's Fine-Grained Table & Page Latching
Nemo replaces database-wide file locks with localized reader-writer latches (`std::shared_mutex`):
- **Concurrent Readers**: Unlimited readers can read from Table A, Table B, and Table C simultaneously.
- **Concurrent Writers**: Worker 1 writing to Table A and Worker 2 writing to Table B execute at the exact same physical nanosecond without blocking or waiting.
- **Zero Collision Failures**: Threads queue gracefully on localized CPU mutexes rather than aborting with lock collision errors. In torture benchmarks, Nemo sustained **68,403 ops/s with 0 errors**, while SQLite suffered **55 lock collision errors** at only **2,230 ops/s**.

### Multi-Version Concurrency Control (MVCC) & Snapshot Isolation
- Every active transaction is assigned a monotonically increasing Transaction ID (`TxID`).
- Modifying transactions append new tuple versions while retaining older tuple versions in memory undo chains.
- Read queries inspect only tuples whose creation `TxID` is less than or equal to their snapshot boundary, guaranteeing reproducible reads without blocking active concurrent mutators.

### Parsing Outside the Lock (Micro-Lock Duration)
In Nemo's `INSERT` implementation, parameter parsing and token slicing are executed **before** the table write lock is acquired:
- `parse_simple_values()` slices SQL strings and allocates values while no locks are held.
- The table `write_lock` is held only for the microsecond duration required to advance counters and update the memory slot directory.

---

## 7. Query Engine Pipeline & Optimization Algorithms

### Fast-Path Lexical Classifier
Nemo eliminates the overhead of full SQL grammar parsing for common queries:
- Scans query prefixes to route directly to specialized execution engines:
  - `EXPLAIN` $\rightarrow$ CBO Optimizer Tree
  - `ANALYZE` $\rightarrow$ Table Statistic Sampler
  - `SELECT ... JOIN` $\rightarrow$ Hash Join Engine
  - `SELECT ... OVER` $\rightarrow$ Window Engine
  - `SELECT COUNT/SUM/AVG/MIN/MAX` $\rightarrow$ Vectorized SIMD Aggregator
  - `SELECT func(...)` $\rightarrow$ Pre-compiled Scalar Extractor

### Cost-Based Query Optimizer (CBO) & EXPLAIN
Nemo's optimizer analyzes table distributions collected via `ANALYZE TABLE <name>`:
- **Cardinality Estimation**: Calculates selectivity based on table row counts and distinct value distributions.
- **Access Path Selection**: Compares cost of full table scan vs B+ Tree index range scan.
- **Physical Plan Tree**: Inspectable using `EXPLAIN <query>`.

### Vectorized SIMD Aggregation Engine (AVX2 / NEON)
When executing aggregate queries (`COUNT`, `SUM`, `AVG`, `MIN`, `MAX`):
- Instead of processing one row at a time with virtual function calls, Nemo processes data in contiguous vector blocks.
- Employs CPU vector registers to compute running sums, minimums, and maximums in parallel.
- **Benchmark Speedup**: **5,409 queries/sec in Nemo vs 58 queries/sec in SQLite (93.66x faster)**.

### In-Memory Hash Join with Selective Column Projection
When executing `SELECT ... FROM t1 JOIN t2 ON t1.key = t2.key`:
1. **Build Phase**: Scans the smaller table (Build input) and builds an in-memory hash table on the join key.
2. **Probe Phase**: Streams the probe table, looks up join keys in the hash table with O(1) expected time, and emits joined tuples.
3. **Selective Column Projection**: Automatically parses projected columns in the `SELECT` clause, copying only requested attributes instead of full rows, reducing memory allocations by up to 85%.
4. **Benchmark Speedup**: **4,542 joins/sec in Nemo vs 1,565 joins/sec in SQLite (2.90x faster)**.

### Pre-Compiled Scalar Data Extraction
Scalar data extractions (`JSON_EXTRACT`, `UPPER`, `LOWER`, `ROUND`, etc.) utilize pre-compiled function signatures (`CompiledFuncType`):
- Tokenization, column indices, and path strings are resolved once prior to iterating rows.
- Eliminates regex and string parsing inside per-row evaluation loops.
- **Benchmark Speedup**: **4,205 ops/sec in Nemo vs 1,589 ops/sec in SQLite (2.65x faster)**.

### Window Function Ranking Engine
Supports `ROW_NUMBER()` and `RANK() OVER (ORDER BY <col> [ASC|DESC])`:
- Extracts order keys and row references.
- Executes in-memory sorting using `std::stable_sort`.
- Assigns dense sequential row numbers and ranking ranks with ties handling.
- **Benchmark Speedup**: **329 ops/sec in Nemo vs 30 ops/sec in SQLite (11.05x faster)**.

### Direct Columnar Extraction API
Applications needing batch columnar analytical access can invoke:
```python
conn.extract_columnar("financial_accounts", ["id", "username", "balance"])
```
- Completely bypasses row-oriented Python tuple wrapping.
- Extracts columns directly as homogeneous contiguous Python arrays/lists.
- **Benchmark Speedup**: **1,983 ops/sec in Nemo vs 13 ops/sec in SQLite (152.75x faster)**.

---

## 8. Write-Ahead Logging (WAL) & Crash Recovery

### WAL Segment Architecture
Write operations append log records to `.nm-wal` files before dirty pages are flushed to the `.nm` file:
- Each log record contains: `LSN` (Log Sequence Number), `TxID`, `Type` (BEGIN, COMMIT, ABORT, INSERT, UPDATE, DELETE), `TableID`, `RID`, and Before/After binary images.

### ARIES-Style Three-Phase Recovery
When opening a database after an abnormal shutdown or power failure:
1. **Analysis Phase**: Scans the WAL forward from the last checkpoint to identify dirty pages in the buffer pool and determine active transactions that did not commit.
2. **REDO Phase**: Replays all logged changes forward to bring the database state up to the exact moment of the crash.
3. **UNDO Phase**: Scans backward and rolls back all uncommitted transactions, restoring complete ACID consistency.

### Checkpointing Engine
The Checkpoint Manager periodically merges committed WAL changes into the main `.nm` file:
- Flushes dirty pages from the buffer pool to disk.
- Truncates and resets the WAL file, preventing unbounded log growth.

---

## 9. Subsystem Architectural Comparison (Nemo vs. SQLite)

| Architectural Dimension | SQLite 3 (.db) | Nemo Database Engine (.nm) |
| :--- | :--- | :--- |
| **Locking Granularity** | Database-wide filesystem lock | Fine-grained table and page mutexes |
| **Simultaneous Writers** | Strictly 1 writer at a time | Multiple writers concurrently across tables |
| **Concurrency Under Load** | Collides with `database is locked` | **0 lock collisions** (68,403 ops/s) |
| **Bulk Ingestion Speed** | 447,739 rows/s (Pure DB) | **31,700,343 rows/s (Pure DB)** (70.8x faster) |
| **One-by-One Single Writes** | 177,385 rows/s | **255,817 rows/s** (1.44x faster) |
| **Streaming Retrieval** | 468,922 rows/s | **2,706,618 rows/s** (5.77x faster) |
| **Vectorized Aggregates** | 58 queries/s (row iteration) | **5,409 queries/s (SIMD parallel)** (93.66x faster) |
| **Hash Joins** | 1,565 joins/s | **4,542 joins/s (projected hash join)** (2.90x faster) |
| **Window Functions** | 30 ops/s | **329 ops/s** (11.05x faster) |
| **Direct Columnar Extract** | 13 ops/s | **1,983 ops/s** (152.75x faster) |
| **Queue / Stack Popping** | 1,333,529 pops/s | **16,372,507 pops/s** (12.28x faster) |
| **Native Datatypes** | 5 dynamic storage classes | **30 strongly typed native datatypes** |
| **Built-in SQL Functions** | Basic scalar library (~30) | **70+ native SQL functions** |
| **Python Bindings** | CTypes / C-API string conversions | **Zero-copy C++20 PyBind11 bindings** |
| **Storage Cleanliness** | Leaves WAL & SHM files on disk | **Zero sidecars, instant clean teardown** |

---

## 10. Further Reading & Runnable Example Suite

- **[Master README](README.md)**: Project overview, performance benchmarks, and quick start guides.
- **[Nemo Python Examples Suite (50 Standalone Scripts)](example/README.md)**: 50 self-contained, runnable Python programs demonstrating every architectural concept, datatype, query engine feature, and concurrency pattern.
- **[SQL Built-in Functions Reference](docs/sql_functions.md)**: Reference for all 70+ built-in SQL scalar and extraction functions.
- **[SQL Syntax & Grammar](docs/sql.md)**: DDL, DML, auto-increment sequences, and Cost-Based Optimizer commands.
- **[Synchronous Python Connector](docs/syncconnector.md)** & **[Asynchronous Python Connector](docs/asyncconnector.md)**: Python client references.

