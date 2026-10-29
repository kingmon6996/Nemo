# Nemo Benchmark Suites & Comparative Performance

This document describes the benchmarking methodology, execution instructions, and architectural scorecards for **Nemo** (`nemo-py`).

---

## Benchmark Suites Overview

| Benchmark Suite | Purpose / Scope | Target Records | Key Dimensions Evaluated |
| :--- | :--- | :--- | :--- |
| **[`test.py`](#1-comparative-benchmark-suite-testpy)** | **Nemo vs SQLite Head-to-Head** | 500,000 bulk + 25,000 single writes | 5 Tables, all 30 datatypes, 16 concurrent threads, multi-function scorecard. |
| **[`test2.py`](#2-20-million-record--feature-suite-test2py)** | **Extreme Scale & SQL Engine Capabilities** | 20,000,000 records | 20M Ingestion, 52 built-in functions, 11-stage advanced SQL benchmark. |
| **[`benchmark_speedguard.py`](#3-speed-guard-architectural-audit)** | **Zero-Overhead Verification** | 100,000 records | SIMD vectoring, Robin Hood grouping, cache-conscious joins, savepoint latency. |

---

## 1. Comparative Benchmark Suite (`test.py`)

Compares Nemo (.nm) directly against SQLite (.db) under identical data generation, transaction boundaries, and concurrent worker contention.

### Running `test.py`

```bash
# Syntax: python test.py [target_dir] [database_name] [bulk_per_table] [threads] [one_by_one_per_table]
python test.py dataset peak_bench 100000 16 5000
```

### Parameters
- `dataset`: Working directory for temporary database files.
- `peak_bench`: Database base name.
- `100000`: Bulk rows per table (5 tables = 500,000 total rows).
- `16`: Number of simultaneous concurrent worker threads.
- `5000`: One-by-one single inserts per table (25,000 total writes).

### Head-to-Head Comparative Scorecard

```
==================================================================================================
ULTIMATE MULTI-FORMAT & PEAK CONCURRENCY BENCHMARK REPORT (NEMO VS SQLITE)
==================================================================================================
Benchmarked Dimension / Metric       | Nemo (.nm)                   | SQLite (.db)                | Verdict / Speedup
--------------------------------------------------------------------------------------------------------------------------
Total Bulk Rows (5 Tables)           | 500,000                      | 500,000                     | Equal Test Data
Total One-By-One Rows (5 Tables)     | 25,000                       | 25,000                      | Equal Test Data
Active Concurrency Threads           | 16                           | 16                          | Equal Contention
5-Table Bulk Insert (Pure DB) (r/s)  | 31,700,343                   | 447,739                     | Nemo is 70.8x faster
5-Table Bulk Insert (w/ PyGen) (r/s) | 322,043                      | 183,198                     | Nemo is 1.76x faster
One-By-One Single Insert (rows/s)    | 255,817                      | 177,385                     | Nemo is 1.44x faster
5-Table Streaming Fetch (rows/s)     | 2,706,618                    | 468,922                     | Nemo is 5.77x faster
Point Query (lookups/s)              | 100,125                      | 48,080                      | Nemo is 2.08x faster
Range Query Scans (ranges/s)         | 18,193                       | 4,944                       | Nemo is 3.68x faster
Vectorized Aggregates (queries/s)    | 5,409                        | 58                          | Nemo is 93.66x faster
In-Memory Hash Joins (joins/s)       | 4,542                        | 1,565                       | Nemo is 2.90x faster
Data Extraction (JSON/Text/Math)     | 4,205                        | 1,589                       | Nemo is 2.65x faster
Window Functions (ROW_NUMBER/RANK)   | 329                          | 30                          | Nemo is 11.05x faster
Columnar Data Extraction (ops/s)     | 1,983                        | 13                          | Nemo is 152.75x faster
Atomic Bulk Popping (pops/s)         | 16,372,507                   | 1,333,529                   | Nemo is 12.28x faster
All-At-Once Concurrency (ops/s)      | 68,403                       | 2,230                       | Nemo is 30.67x faster
Concurrency Lock/Busy Errors         | 0                            | 55                          | Nemo 100% Reliability
5-Table Truncate Time                | 10.74 ms                     | 93.18 ms                    | Nemo is 8.68x faster
Final Disk Space Used                | 0.00 B                       | 92.71 MB                    | Clean Teardown
==================================================================================================
```

### SQL Function & Expression Comparison

`test.py` outputs a side-by-side SQL function throughput scorecard:

```
==================================================================================================
COMPREHENSIVE SQL FUNCTION & EXPRESSION SCORECARD (NEMO VS SQLITE)
==================================================================================================
SQL Function / Expression          | Nemo (.nm)             | SQLite (.db)           | Speedup       
--------------------------------------------------------------------------------------------------
Math: ABS & ROUND                  |              541 QPS   |            2,903 QPS   | SQLite 5.37x  
String: UPPER, LOWER, LENGTH       |              700 QPS   |            2,235 QPS   | SQLite 3.19x  
JSON: JSON_EXTRACT                 |            5,232 QPS   |            1,993 QPS   | Nemo 2.62x    
Utility: COALESCE & NULLIF         |              299 QPS   |            4,239 QPS   | SQLite 14.16x 
Logic: CASE WHEN                   |              178 QPS   |            3,915 QPS   | SQLite 22.00x 
Predicate: IN (List)               |            4,041 QPS   |            9,402 QPS   | SQLite 2.33x  
Predicate: BETWEEN & LIKE          |            1,369 QPS   |            4,823 QPS   | SQLite 3.52x  
Grouping: GROUP BY                 |            9,972 QPS   |            7,291 QPS   | Nemo 1.37x    
Grouping: GROUP BY + HAVING        |           10,086 QPS   |            7,359 QPS   | Nemo 1.37x    
Relational: LEFT JOIN              |            6,692 QPS   |            4,023 QPS   | Nemo 1.66x    
Set Ops: UNION ALL                 |            2,326 QPS   |            7,206 QPS   | SQLite 3.10x  
Set Ops: UNION (Deduplicated)      |            1,133 QPS   |            3,950 QPS   | SQLite 3.49x  
Pagination: DISTINCT & OFFSET      |           10,818 QPS   |           17,070 QPS   | SQLite 1.58x  
==================================================================================================
```

---

## 2. 20-Million Record & Feature Suite (`test2.py`)

Stress-tests extreme table volumes across all 30 datatypes and executes an 11-stage benchmark covering every newly added SQL engine feature:

### Running `test2.py`

```bash
# Run full 20 Million record suite (default)
python test2.py

# Or run with custom scale for quick verification (e.g., 100,000 records, 50,000 batch size)
python test2.py 100000 50000
```

### Output Breakdown
- **Pure C++ Ingestion**: Measures throughput of raw slotted page byte insertion.
- **52 Built-in Function Tests**: Tests functions across JSON, ARRAY, DICT, STRING, MATH, RANGE, TIME, BINARY, BITWISE, NETWORK, and SEQUENCE categories.
- **11-Stage Advanced SQL Benchmark**:
  - SIMD Vector Aggregates (`COUNT`, `SUM`, `AVG`, `MIN`, `MAX`)
  - `GROUP BY` Single Key & Multi-Column
  - `GROUP BY` with `HAVING` condition filtering
  - `SELECT DISTINCT` Deduplication
  - Zero-Copy Pagination (`OFFSET` & `FETCH FIRST`)
  - Branchless `CASE WHEN` Evaluation
  - Hash-Accelerated `IN (...)` Predicates
  - Cache-Conscious `LEFT JOIN`
  - Streaming Set Operations (`UNION ALL`)
  - Direct Columnar Analytical API (`extract_columnar`)
  - Microsecond Savepoints and Checkpoints (`SAVEPOINT`, `ROLLBACK TO SAVEPOINT`)

---

## 3. Speed-Guard Architectural Audit

Verifies that new SQL features do not introduce latency regressions against plain scans:

```bash
python benchmarks/benchmark_speedguard.py
```

### Audit Metrics
- **Bulk Ingestion**: >= 30,000,000 rows/s (Pure DB)
- **Point Lookups**: >= 100,000 QPS
- **Atomic Bulk Popping**: >= 16,000,000 pops/s
- **Query Latency Ratio vs Plain Scan**: Guaranteed < 5.0x baseline for `GROUP BY`, `LEFT JOIN`, and `UNION`.

