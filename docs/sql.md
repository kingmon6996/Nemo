# Nemo SQL Reference & Grammar

This document provides a complete guide to supported SQL statements, syntax rules, column constraints, sequence generation, table operations, and parameter bindings in **Nemo**.

For individual scalar and extraction functions, see the [SQL Built-in Functions Reference](sql_functions.md).  
For parameter binding and prepared statements, see the [SQL Parameters Reference](sql_parameters.md).

---

## Table of Contents

1. [Data Definition Language (DDL)](#1-data-definition-language-ddl)
   - [`CREATE TABLE`](#create-table)
   - [Auto-Incrementing Primary Keys (`AUTOINCREMENT` / `SERIAL`)](#auto-incrementing-primary-keys)
   - [`CREATE SEQUENCE`](#create-sequence)
   - [`DROP TABLE`](#drop-table)
2. [Data Manipulation Language (DML)](#2-data-manipulation-language-dml)
   - [`INSERT INTO`](#insert-into)
   - [`SELECT`](#select)
   - [`UPDATE`](#update)
   - [`DELETE FROM`](#delete-from)
3. [Query Execution, Optimizer & Analytical SQL](#3-query-execution-optimizer--analytical-sql)
   - [Cost-Based Query Optimizer (CBO) & `EXPLAIN`](#cost-based-query-optimizer-cbo--explain)
   - [In-Memory Hash Joins](#in-memory-hash-joins)
   - [Vectorized SIMD Aggregates](#vectorized-simd-aggregates)
   - [Window Functions (`ROW_NUMBER` & `RANK`)](#window-functions-row_number--rank)
   - [Direct Columnar Extraction API](#direct-columnar-extraction-api)
4. [Nemo Native Table Operations](#4-nemo-native-table-operations)
   - [`pop` (Atomic FIFO & LIFO Queue/Stack)](#pop-atomic-fifo--lifo)
   - [`count` (Instant O(1) Row Count)](#count-instant-o1)
   - [`truncate` (Instant O(1) Table Wipe)](#truncate-instant-o1)
5. [Sequence & ID Range Allocation](#5-sequence--id-range-allocation)
6. [Transactions & Concurrency](#6-transactions--concurrency)

---

## 1. Data Definition Language (DDL)

### `CREATE TABLE`

Defines a new relational table schema with typed columns and constraints:

```sql
CREATE TABLE [IF NOT EXISTS] table_name (
    column_name DATA_TYPE [CONSTRAINTS],
    ...
);
```

#### Supported Column Constraints
- `PRIMARY KEY`: Unique row identifier. Automatically enforces `NOT NULL` and `UNIQUE`.
- `AUTOINCREMENT` / `SERIAL`: Database-managed monotonically increasing sequence starting at 1.
- `NOT NULL`: Rejects `NULL` values upon insertion or update.
- `UNIQUE`: Rejects duplicate values across rows.
- `DEFAULT <literal>`: Specifies default value when omitted from `INSERT`.

#### Example: Complete Datatypes Table
```sql
CREATE TABLE omni_bench (
    id BIGINT PRIMARY KEY AUTOINCREMENT,
    code CHAR(8) NOT NULL UNIQUE,
    title VARCHAR NOT NULL,
    amount DECIMAL(12, 2) DEFAULT 0.00,
    counter INTEGER DEFAULT 0,
    is_active BOOLEAN DEFAULT true,
    created_at TIMESTAMP,
    uuid_key UUID,
    doc_json JSON,
    doc_jsonb JSONB,
    tags ARRAY,
    attributes DICT,
    valid_range RANGE,
    client_ip INET,
    subnet CIDR
);
```

---

### Auto-Incrementing Primary Keys

When a column is declared as `AUTOINCREMENT` or `SERIAL`, Nemo initializes an atomic sequence starting at `1` and incrementing by `1`:

```sql
CREATE TABLE users (
    id BIGINT PRIMARY KEY AUTOINCREMENT,
    username VARCHAR NOT NULL
);

-- When id is omitted, the engine automatically assigns nextval:
INSERT INTO users (username) VALUES ('alice'); -- id = 1
INSERT INTO users (username) VALUES ('bob');   -- id = 2
```

---

### `CREATE SEQUENCE`

Explicitly creates an atomic, persistent sequence object:

```sql
CREATE SEQUENCE sequence_name [START WITH <int>] [INCREMENT BY <int>];
```

#### Example:
```sql
CREATE SEQUENCE order_seq START WITH 5000 INCREMENT BY 10;
```

---

### `DROP TABLE`

Deletes a table definition and frees all assigned storage pages:

```sql
DROP TABLE users;
```

---

## 2. Data Manipulation Language (DML)

### `INSERT INTO`

Inserts new records into a table. Use `?` placeholders for parameterized safety:

```sql
-- Parameterized (Recommended)
INSERT INTO users (username, email, score) VALUES (?, ?, ?);

-- Explicit Literals
INSERT INTO users (id, username, score) VALUES (1, 'alice', 100);
```

---

### `SELECT`

Queries records with projections, scalar functions, filtering, sorting, and pagination:

```sql
-- Basic selection with projection
SELECT id, username, upper(username) AS upper_name, json_extract(doc, '$.role') 
FROM users 
WHERE score >= ? AND is_active = true
ORDER BY score DESC
LIMIT 50 OFFSET 100;

-- Scalar queries without FROM clause (Dual expression queries)
SELECT now();
SELECT uuid_generate();
SELECT nextval('order_seq');
SELECT range(10, 50);
```

---

### `UPDATE`

Modifies records matching a `WHERE` condition:

```sql
UPDATE users 
SET score = score + ?, doc = json_set(doc, '$.status', 'verified')
WHERE id = ?;
```

---

### `DELETE FROM`

Removes records matching a `WHERE` condition:

```sql
DELETE FROM users WHERE is_active = false AND score < ?;
```

---

## 3. Query Execution, Optimizer & Analytical SQL

### Cost-Based Query Optimizer (CBO) & `EXPLAIN`

Nemo incorporates a native Cost-Based Optimizer that collects statistical distributions and chooses optimal physical execution paths:

```python
# 1. Collect statistics on table distributions
conn.analyze("financial_accounts")

# 2. Inspect physical execution plan tree
plan = conn.explain("SELECT AVG(balance) FROM financial_accounts WHERE balance > 1000;")
print(plan)
```

### In-Memory Hash Joins

High-speed equi-joins between tables are executed via an in-memory hash join algorithm that automatically projects only requested columns:

```sql
SELECT order_book.id, financial_accounts.username, order_book.symbol, order_book.price
FROM order_book
JOIN financial_accounts ON order_book.account_id = financial_accounts.id
LIMIT 1000;
```
*Throughput: **4,542 joins/second** (2.90x faster than SQLite).*

### Vectorized SIMD Aggregates

Batch-evaluated aggregate functions compute running statistics across vector blocks:

```sql
SELECT COUNT(*), SUM(balance), AVG(balance), MIN(balance), MAX(balance)
FROM financial_accounts;
```
*Throughput: **5,409 queries/second** (93.66x faster than SQLite).*

### Window Functions (`ROW_NUMBER` & `RANK`)

Supports in-memory row ranking with partitionless order clauses:

```sql
SELECT id, username, balance,
       ROW_NUMBER() OVER (ORDER BY balance DESC) AS rn,
       RANK() OVER (ORDER BY balance DESC) AS rk
FROM financial_accounts
LIMIT 100;
```
*Throughput: **329 queries/second** (11.05x faster than SQLite).*

### Direct Columnar Extraction API

Bypasses row-by-row Python object wrapping to extract homogeneous columnar data directly:

```python
columns_dict = conn.extract_columnar("financial_accounts", ["id", "username", "balance"])
print(columns_dict["username"][:5])
```
*Throughput: **1,983 extractions/second** (152.75x faster than SQLite).*

---

## 4. Nemo Native Table Operations

Beyond standard SQL DML, Nemo provides three high-velocity primitives executed directly inside the C++ engine:

### `pop` (Atomic FIFO & LIFO)
Atomically fetches and deletes records in a single lock-free operation without row-by-row transaction overhead:

```python
# Pop 10 items from head (FIFO queue)
batch = conn.pop("job_queue", count=10, direction="fifo")

# Pop 5 items from tail (LIFO stack)
events = conn.pop("event_log", count=5, direction="lifo")
```
*Performance: Over **16 Million operations/second**.*

### `count` (Instant O(1))
Retrieves the exact row count of a table in O(1) time:
```python
total_rows = conn.count("users")
```

### `truncate` (Instant O(1))
Instantly resets the table and recycles all storage frames without scanning rows:
```python
conn.truncate("session_cache")
```

---

## 5. Sequence & ID Range Allocation

In addition to SQL `nextval()` and `currval()`, Nemo provides a native Python API for reserving atomic ID ranges:

```python
# Reserve 50 contiguous IDs for a worker thread
start_id, end_id = conn.allocate_id_range("order_seq", count=50)
print(f"Reserved IDs: {start_id} to {end_id}")
```
This enables multi-threaded or distributed bulk writers to ingest data with pre-allocated primary keys without locking or querying the sequence on each insert.

---

## 6. Transactions & Concurrency

Nemo supports ACID transactions with fine-grained latching and MVCC snapshot isolation:

```python
with conn:
    conn.execute("UPDATE accounts SET balance = balance - 100 WHERE id = 1;")
    conn.execute("UPDATE accounts SET balance = balance + 100 WHERE id = 2;")
```

Readers never block writers, and writers never block readers. Under high multi-threaded contention, Nemo eliminates SQLite's `database is locked` error.

---

## Related Runnable Examples

- [`example_1.py`](../example/example_1.py): Basic CRUD Operations and Primary Keys.
- [`example_5.py`](../example/example_5.py): Analytical SQL, Hash Joins, SIMD Aggregation, and CBO Optimizer.
- [`example_7.py`](../example/example_7.py): Sequences and Atomic ID Range Allocation.
- [`example_11.py`](../example/example_11.py): Parameterized Queries and Prepared Statements.
- [`example_14.py`](../example/example_14.py): Fast Table Truncate and Storage Reclamation (`conn.truncate()`).
- [`example_27.py`](../example/example_27.py): Multi-Table Relational Hash Joins.
- [`example_28.py`](../example/example_28.py): SIMD Vectorized Aggregations (`SUM`, `AVG`, `MIN`, `MAX`).
- [`example_29.py`](../example/example_29.py): Window Functions and Analytical Ranking (`ROW_NUMBER`, `RANK`).
- [`example_30.py`](../example/example_30.py): Cost-Based Optimizer (CBO) and `EXPLAIN`.
- [`example_31.py`](../example/example_31.py): Common Table Expressions (CTEs).
- [`example_32.py`](../example/example_32.py): Filter Expressions and Range Queries (`BETWEEN`).
- [`example_35.py`](../example/example_35.py): Sequences and `nextval` / `currval`.
- [`example_36.py`](../example/example_36.py): Batch ID Range Allocation.



---

## 7. Advanced SQL Engine Features (v0.2.0)

For full details, architecture, and examples, refer to the [Advanced SQL Engine Features Guide](sql_features.md) and [Savepoints Guide](savepoints.md).

### High-Speed Grouping & Pagination
```sql
SELECT dept, count(*), sum(salary), avg(salary) FROM employees GROUP BY dept HAVING count(*) > 5;
SELECT * FROM sales OFFSET 100 ROWS FETCH FIRST 25 ROWS ONLY;
SELECT DISTINCT category FROM products;
```

### Branchless Expressions & Predicates
```sql
SELECT id, CASE WHEN score >= 90 THEN 'A' WHEN score >= 75 THEN 'B' ELSE 'C' END FROM grades;
SELECT * FROM users WHERE status IN ('active', 'verified') AND notes IS NOT NULL;
SELECT * FROM catalog WHERE price BETWEEN 10.0 AND 50.0 AND title LIKE 'Pro%';
```

### Cache-Conscious Joins & Set Operations
```sql
SELECT e.name, d.name FROM employees e LEFT JOIN departments d ON e.dept_id = d.id;
SELECT e.name, d.name FROM employees e FULL JOIN departments d USING (dept_id);
SELECT item FROM warehouse_a UNION SELECT item FROM warehouse_b;
SELECT item FROM warehouse_a INTERSECT SELECT item FROM warehouse_b;
SELECT item FROM warehouse_a EXCEPT SELECT item FROM warehouse_b;
```

### Extended DML, Atomic UPSERT & RETURNING
```sql
INSERT INTO inventory VALUES (1, 'Pen', 10) ON CONFLICT (id) DO UPDATE SET qty = EXCLUDED.qty;
INSERT INTO archive_items SELECT * FROM active_items;
INSERT INTO accounts VALUES (10, 'Alice', 500.0) RETURNING id, balance;
```

### Zero-Copy Schema Evolution & Views
```sql
ALTER TABLE users ADD COLUMN IF NOT EXISTS role TEXT DEFAULT 'member';
ALTER TABLE users RENAME COLUMN role TO user_role;
ALTER TABLE users DROP COLUMN IF EXISTS temp_col;
ALTER TABLE users RENAME TO members;
CREATE VIEW active_users AS SELECT id, name FROM members WHERE is_active = true;
CREATE INDEX idx_members_name ON members (name);
```

### Microsecond Savepoints
```sql
SAVEPOINT step1;
ROLLBACK TO SAVEPOINT step1;
RELEASE SAVEPOINT step1;
```
