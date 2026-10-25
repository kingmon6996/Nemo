# Nemo Peak Performance & Concurrency Benchmarks

This directory and guide document the complete benchmarking methodology, test harness suites, and comparative performance results of **Nemo (.nm)** versus **SQLite (.db)**.

---

## 1. Executive Benchmark Summary (Nemo vs SQLite)

Tested on **500,000 bulk rows** + **25,000 one-by-one single inserts** across 5 heterogeneous multi-format tables with **16 simultaneous concurrent threads**:

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

---

## 2. Tested Datatypes & Tables

The benchmark runs against five real-world schemas covering **all 30 native SQL datatypes**:

1. **`omni_records`** (All 30 Datatypes):
   - `BOOL`, `SMALLINT`, `INT`, `BIGINT`, `REAL`, `DOUBLE`, `DECIMAL`, `CHAR`, `VARCHAR`, `TEXT`, `BYTEA`, `DATE`, `TIME`, `TIMESTAMP`, `TIMESTAMPTZ`, `INTERVAL`, `UUID`, `JSON`, `JSONB`, `DICT`, `ARRAY`, `ENUM`, `BIT`, `VARBIT`, `INET`, `CIDR`, `MACADDR`, `RANGE`, `NULL`, and Auto-increment Primary Key `ID`.
2. **`financial_accounts`**:
   - Accounts, balances, risk ratings, JSON preferences, active status, creation timestamps.
3. **`order_book`**:
   - High-frequency limit order records, symbols, prices, quantities, side flags, account foreign keys.
4. **`crypto_transactions`**:
   - Blockchain transactions, tx hash, from/to wallet addresses, token amounts, gas fees, block numbers.
5. **`iot_telemetry_stream`**:
   - Sensor IDs, machine names, temperature readings, vibration metrics, JSON metadata, reading timestamps.

---

## 3. How to Run the Benchmark Suites

### Running the Comparative Benchmark (`test.py`)
To run the automated side-by-side benchmark comparing Nemo with SQLite:

```bash
# Syntax: python test.py [target_dir] [database_name] [bulk_per_table] [threads] [one_by_one_per_table]
python test.py dataset peak_bench 100000 16 5000
```

Parameters:
- `dataset`: Working directory for temporary test database files.
- `peak_bench`: Database base name.
- `100000`: Number of bulk rows per table (500,000 total rows across 5 tables).
- `16`: Number of simultaneous concurrent worker threads.
- `5000`: Number of one-by-one single inserts per table (25,000 total individual single writes).

### Running the 20-Million Dataset & 52 SQL Function Suite (`test2.py`)
To test extreme scale (20,000,000 rows) and verify all 52 built-in SQL functions across 10 functional categories:

```bash
python test2.py
```

Outputs a detailed microsecond execution breakdown for:
- 20 Million row pure C++ ingestion
- Schema creation
- Array vector operations
- JSON/JSONB path traversal
- Math, String, Range, Network, Sequence, and Time functions
