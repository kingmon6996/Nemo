# Nemo Python SDK Examples Guide

This directory contains **50 comprehensive, self-contained executable examples** covering every operational capability, relational feature, SQL function, concurrency model, and analytical pipeline in the **Nemo** database engine (`nemo-py`).

---

## Complete Index of Examples (1 – 50)

### Category 1: Core Operations & Table Schemas
| Example | Topic / Feature Demonstrated | Run Command |
| :--- | :--- | :--- |
| **`example_1.py`** | Basic synchronous CRUD, auto-increment keys, single inserts, batch `executemany`, updates, deletes, and instant `truncate()`. | `python example/example_1.py` |
| **`example_2.py`** | Asynchronous operations with `asyncio`, coroutine workers, `async with`, async batching, and `AsyncConnectionPool`. | `python example/example_2.py` |
| **`example_3.py`** | Demonstration of all 30 native SQL datatypes (Primitives, Collections, Temporals, Arrays, Dicts, JSON, UUID, IP/CIDR, Ranges). | `python example/example_3.py` |
| **`example_4.py`** | Built-in SQL functions for JSON extraction, array manipulation, dict lookup, string, math, and range boundaries. | `python example/example_4.py` |
| **`example_5.py`** | High-speed in-memory hash joins with selective column projection, SIMD aggregates, window ranking, and CBO query planning. | `python example/example_5.py` |
| **`example_6.py`** | High-velocity FIFO queue and LIFO stack operations using native atomic popping (`conn.pop()`) at 7M+ ops/second. | `python example/example_6.py` |
| **`example_7.py`** | Database sequences (`nextval`, `currval`) and atomic batch ID range reservation (`allocate_id_range`) for multi-worker ingestion. | `python example/example_7.py` |
| **`example_8.py`** | Direct columnar data extraction API (`conn.extract_columnar()`) bypassing row-by-row tuple wrapping for analytical pipelines. | `python example/example_8.py` |
| **`example_9.py`** | ACID transactions, automatic rollbacks on exceptions, manual commits/rollbacks, and snapshot isolation. | `python example/example_9.py` |
| **`example_10.py`** | Multi-threaded synchronous connection pooling, thread-safe lease acquisition, and concurrent stress testing with zero lock collisions. | `python example/example_10.py` |

### Category 2: Prepared Statements & Metadata Reflection
| Example | Topic / Feature Demonstrated | Run Command |
| :--- | :--- | :--- |
| **`example_11.py`** | Parameterized queries (`?`), SQL injection prevention, and reusable prepared statements (`conn.prepare()`). | `python example/example_11.py` |
| **`example_12.py`** | Query performance diagnostics and execution profiling via `result.statistics` (buffer hits, planning/execution times). | `python example/example_12.py` |
| **`example_13.py`** | Schema reflection and column metadata inspection (`result.columns`, types, table origin, nullability). | `python example/example_13.py` |
| **`example_14.py`** | High-speed table truncation and instantaneous O(1) storage reclamation (`conn.truncate()`). | `python example/example_14.py` |

### Category 3: Datatypes & Rich Encodings
| Example | Topic / Feature Demonstrated | Run Command |
| :--- | :--- | :--- |
| **`example_15.py`** | Numeric datatypes deep-dive (`TINYINT`, `SMALLINT`, `INTEGER`, `BIGINT`, `REAL`, `DOUBLE`, boundary testing). | `python example/example_15.py` |
| **`example_16.py`** | Exact financial decimals (`DECIMAL`), preventing binary floating-point rounding errors in banking ledgers. | `python example/example_16.py` |
| **`example_17.py`** | Text, international Unicode, and raw cryptographic binary blobs (`VARCHAR`, `TEXT`, `BYTEA`). | `python example/example_17.py` |
| **`example_18.py`** | Temporal types and time-series scheduling (`DATE`, `TIME`, `TIMESTAMP`, `TIMESTAMPTZ`, `INTERVAL`). | `python example/example_18.py` |
| **`example_19.py`** | Semi-structured JSON and binary JSONB document storage, querying, and updates. | `python example/example_19.py` |
| **`example_20.py`** | Native structured collection types: ordered dynamic `ARRAY` and key-value `DICT`. | `python example/example_20.py` |
| **`example_21.py`** | System & networking types: `UUID`, IP addresses (`INET`, `CIDR`), MAC addresses, and `RANGE` intervals. | `python example/example_21.py` |

### Category 4: In-Built SQL Functions
| Example | Topic / Feature Demonstrated | Run Command |
| :--- | :--- | :--- |
| **`example_22.py`** | Native JSON SQL functions: `json_extract`, `json_length`, `json_contains`, and `json_set`. | `python example/example_22.py` |
| **`example_23.py`** | Collection SQL functions: `array_get`, `array_sum`, `array_avg`, `array_append`, `dict_get`, `dict_has_key`. | `python example/example_23.py` |
| **`example_24.py`** | String SQL functions: `upper`, `lower`, `concat`, `substring`, and `trim`. | `python example/example_24.py` |
| **`example_25.py`** | Mathematical SQL expressions: `abs`, `round`, `sqrt`, `power`, and `mod`. | `python example/example_25.py` |
| **`example_26.py`** | Range, network & utility functions: `range_lower`, `range_upper`, `range_contains`, `host`, `netmask`, `coalesce`. | `python example/example_26.py` |

### Category 5: Relational & Analytical Queries
| Example | Topic / Feature Demonstrated | Run Command |
| :--- | :--- | :--- |
| **`example_27.py`** | Multi-table relational hash joins with predicate pushdown and selective column projections. | `python example/example_27.py` |
| **`example_28.py`** | SIMD vectorized aggregate processing (AVX2/NEON 1024-element chunks for `SUM`, `AVG`, `MIN`, `MAX`). | `python example/example_28.py` |
| **`example_29.py`** | Analytical window ranking functions: `ROW_NUMBER()` and `RANK() OVER (ORDER BY ...)`. | `python example/example_29.py` |
| **`example_30.py`** | Cost-Based Optimizer (CBO) and physical plan tree inspection with `conn.explain()`. | `python example/example_30.py` |
| **`example_31.py`** | Common Table Expressions (CTEs) using `WITH ... AS (...)` for modular queries. | `python example/example_31.py` |
| **`example_32.py`** | Filter expressions and range queries using `BETWEEN`, equality, and compound conditions. | `python example/example_32.py` |

### Category 6: Queues, Stacks & Sequences
| Example | Topic / Feature Demonstrated | Run Command |
| :--- | :--- | :--- |
| **`example_33.py`** | High-velocity background task processing with atomic FIFO queues (`conn.pop(..., 'fifo')`). | `python example/example_33.py` |
| **`example_34.py`** | Document revision undo stack and audit log popping using atomic LIFO operations (`conn.pop(..., 'lifo')`). | `python example/example_34.py` |
| **`example_35.py`** | Standalone atomic database sequences with `nextval('seq')` and `currval('seq')`. | `python example/example_35.py` |
| **`example_36.py`** | Zero-contention parallel ID range reservation blocks using `conn.allocate_id_range()`. | `python example/example_36.py` |

### Category 7: High-Speed Extraction & Enterprise Architecture
| Example | Topic / Feature Demonstrated | Run Command |
| :--- | :--- | :--- |
| **`example_37.py`** | High-speed columnar data extraction into typed `{col_name: [values]}` dictionaries for analytical pipelines. | `python example/example_37.py` |
| **`example_38.py`** | Storage-level pushdown filtering and column projection using `conn.extract()`. | `python example/example_38.py` |
| **`example_39.py`** | ACID transaction safety, context manager auto-commit, and automatic rollback on Python exceptions. | `python example/example_39.py` |
| **`example_40.py`** | End-to-end e-commerce microservice platform integrating catalogs, orders, fulfillment queue, and SIMD analytics. | `python example/example_40.py` |

### Category 8: Advanced Engine Mechanics & Architecture Deep-Dive
| Example | Topic / Feature Demonstrated | Run Command |
| :--- | :--- | :--- |
| **`example_41.py`** | Slotted page storage architecture: 4096-byte blocks, bottom-up record packing, and tombstone slot reuse without fragmentation. | `python example/example_41.py` |
| **`example_42.py`** | Buffer pool CLOCK eviction algorithm, second-chance referencing bits, and warm memory page cache hit ratios. | `python example/example_42.py` |
| **`example_43.py`** | Multi-Version Concurrency Control (MVCC) snapshot isolation: simultaneous readers and writers with zero lock contention. | `python example/example_43.py` |
| **`example_44.py`** | Write-Ahead Logging (WAL) and ARIES crash recovery mechanics: redo logs, undo rollback chains, and transaction durability. | `python example/example_44.py` |
| **`example_45.py`** | Cost-Based Optimizer (CBO) statistical distributions via `conn.analyze()` and physical query plan trees via `conn.explain()`. | `python example/example_45.py` |
| **`example_46.py`** | In-memory projected hash join algorithms: build and probe phases with column projection pushdown. | `python example/example_46.py` |
| **`example_47.py`** | Vectorized SIMD aggregations in 1024-element aligned CPU chunks (AVX2 / NEON) for ultra-fast `SUM`, `AVG`, `MIN`, `MAX`. | `python example/example_47.py` |
| **`example_48.py`** | Contention-free multi-worker partitioning using sequence atomic block reservations via `conn.allocate_id_range()`. | `python example/example_48.py` |
| **`example_49.py`** | B+ Tree indexing anatomy: point query index lookups vs contiguous sibling leaf pointer range scans (`BETWEEN`). | `python example/example_49.py` |
| **`example_50.py`** | Comprehensive master architecture showcase uniting pooling, transactions, hash joins, SIMD, queues, and columnar extraction. | `python example/example_50.py` |

### Category 9: Advanced SQL Engine Features (Phases 1 – 6)
| Example | Topic / Feature Demonstrated | Run Command |
| :--- | :--- | :--- |
| **`example_51.py`** | High-speed multi-column `GROUP BY`, aggregate accumulators (`SUM`, `COUNT`, `MIN`, `MAX`, `AVG`), `HAVING` filters, `SELECT DISTINCT`, and SQL:2008 `OFFSET` / `FETCH FIRST` zero-copy pagination. | `python example/example_51.py` |
| **`example_52.py`** | Branchless expressions & predicates: Searched/Simple `CASE WHEN`, SIMD & hash-accelerated `IN` / `NOT IN`, short-circuit `EXISTS`, `IS NULL`, `BETWEEN`, and `LIKE`. | `python example/example_52.py` |
| **`example_53.py`** | Cache-conscious outer joins (`LEFT`, `RIGHT`, `FULL OUTER`, `CROSS JOIN`, `USING`) and streaming set operations (`UNION`, `UNION ALL`, `INTERSECT`, `EXCEPT`). | `python example/example_53.py` |
| **`example_54.py`** | Extended DML, atomic UPSERT (`ON CONFLICT DO UPDATE SET` / `DO NOTHING`), direct-stream `INSERT ... SELECT`, and pipelined `RETURNING` clauses. | `python example/example_54.py` |
| **`example_55.py`** | Zero-copy schema evolution (`ALTER TABLE ADD/DROP/RENAME COLUMN/TABLE`), lightweight virtual views (`CREATE/REPLACE/DROP VIEW`), and adaptive secondary indexing. | `python example/example_55.py` |
| **`example_56.py`** | Microsecond savepoints, nested transaction checkpoints, and partial rollbacks (`SAVEPOINT`, `ROLLBACK TO SAVEPOINT`, `RELEASE SAVEPOINT`, `txn.savepoint()`). | `python example/example_56.py` |

---

## Running the Examples

Each example is a standalone executable script that creates an isolated local database, executes the scenario, prints diagnostic output, and cleans up its database upon exit:

```bash
# Run any individual example
python example/example_1.py
python example/example_20.py
python example/example_40.py
python example/example_50.py

# Run all 50 examples sequentially
python -c "import subprocess, sys; [subprocess.run([sys.executable, f'example/example_{i}.py'], check=True) for i in range(1, 51)]"
```
