# Nemo Advanced SQL Engine Features (v0.2.0)

This document provides a comprehensive developer and architectural guide to the enterprise-grade SQL features introduced in **Nemo 0.2.0**. All features are built on Nemo's **Speed-Guard Architecture**, guaranteeing zero overhead on Nemo's ultra-low embedded latency and 30M+ rows/s throughput.

---

## Table of Contents

1. [High-Speed Grouping, Aggregation & Pagination](#1-high-speed-grouping-aggregation--pagination)
   - [`GROUP BY` & Running Accumulators](#group-by--running-accumulators)
   - [`HAVING` Condition Filters](#having-condition-filters)
   - [SQL:2008 Zero-Copy Pagination (`OFFSET` & `FETCH FIRST`)](#sql2008-zero-copy-pagination)
   - [Streaming Deduplication (`SELECT DISTINCT`)](#streaming-deduplication-select-distinct)
2. [Branchless Expressions & Predicates](#2-branchless-expressions--predicates)
   - [`CASE ... WHEN ... THEN ... ELSE ... END`](#case-expressions)
   - [`IN (...)` & Subquery Membership](#in-predicates)
   - [Short-Circuit `EXISTS` & `NOT EXISTS`](#exists-and-not-exists)
   - [Three-Valued Logic (`IS NULL` & `IS NOT NULL`)](#is-null-and-is-not-null)
   - [Continuous Range (`BETWEEN`) & Pattern Matching (`LIKE`)](#between-and-like)
3. [Cache-Conscious Joins & Streaming Set Operations](#3-cache-conscious-joins--streaming-set-operations)
   - [Outer Joins (`LEFT`, `RIGHT`, `FULL OUTER`, `CROSS JOIN`)](#outer-joins)
   - [`USING (column)` Syntax](#using-syntax)
   - [Set Operations (`UNION`, `UNION ALL`, `INTERSECT`, `EXCEPT`)](#streaming-set-operations)
4. [Extended DML, Atomic UPSERT & Pipelined RETURNING](#4-extended-dml-atomic-upsert--pipelined-returning)
   - [Direct-Stream `INSERT INTO ... SELECT`](#direct-stream-insert--select)
   - [Atomic `ON CONFLICT DO UPDATE SET` (UPSERT)](#atomic-upsert)
   - [Idempotent `ON CONFLICT DO NOTHING`](#idempotent-on-conflict-do-nothing)
   - [Pipelined `RETURNING` Clause](#pipelined-returning)
5. [Zero-Copy Schema Evolution, Views & Fast Indexing](#5-zero-copy-schema-evolution-views--fast-indexing)
   - [Zero-Downtime `ALTER TABLE`](#zero-downtime-alter-table)
   - [Virtual Views (`CREATE / REPLACE / DROP VIEW`)](#virtual-views)
   - [Adaptive B+ Tree Secondary Indexes (`CREATE / DROP INDEX`)](#secondary-indexes)
6. [Microsecond Savepoints & Nested Transactions](#6-microsecond-savepoints--nested-transactions)
   - [`SAVEPOINT`, `ROLLBACK TO SAVEPOINT`, `RELEASE SAVEPOINT`](#savepoint-grammar)
   - [Python API Integration](#python-api-integration)

---

## 1. High-Speed Grouping, Aggregation & Pagination

### `GROUP BY` & Running Accumulators

Groups rows by one or more columns and computes in-place aggregate statistics (`COUNT`, `SUM`, `AVG`, `MIN`, `MAX`) without boxing values.

```sql
-- Single key grouping
SELECT department, count(*), sum(salary), avg(salary), min(salary), max(salary)
FROM employees
GROUP BY department;

-- Multi-column grouping
SELECT region, rep, count(*), sum(amount)
FROM sales
GROUP BY region, rep;
```

**Architectural Protection**:
- Contiguous aggregate accumulator vectors aligned with CPU cache lines.
- Keys are hashed with 64-bit MurmurHash3 for single-cycle flat bucket lookup.

### `HAVING` Condition Filters

Filters aggregated group buckets post-aggregation without materializing intermediate table copies:

```sql
SELECT department, count(*), sum(salary)
FROM employees
GROUP BY department
HAVING sum(salary) >= 500000;

-- Filter directly on group counts:
SELECT customer, sum(total)
FROM orders
GROUP BY customer
HAVING count(*) > 5;
```

### SQL:2008 Zero-Copy Pagination

Standard SQL:2008 paging semantics bypass unnecessary tuple allocation:

```sql
-- Standard OFFSET and FETCH FIRST
SELECT id, username, email
FROM users
ORDER BY id
OFFSET 100 ROWS FETCH FIRST 25 ROWS ONLY;

-- Traditional LIMIT / OFFSET syntax
SELECT id, username FROM users LIMIT 25 OFFSET 100;
```

### Streaming Deduplication (`SELECT DISTINCT`)

Rejects duplicate tuples via inline deduplication hashing:

```sql
SELECT DISTINCT region, category FROM sales_ledger;
```

---

## 2. Branchless Expressions & Predicates

### `CASE` Expressions

Supports both Searched `CASE` and Simple `CASE` expressions:

```sql
-- Searched CASE
SELECT id, username,
       CASE
           WHEN balance >= 10000.0 THEN 'Platinum'
           WHEN balance >= 2500.0 THEN 'Gold'
           ELSE 'Standard'
       END AS tier
FROM accounts;

-- Simple CASE
SELECT id,
       CASE status
           WHEN 'active' THEN 1
           WHEN 'pending' THEN 2
           ELSE 0
       END AS status_code
FROM accounts;
```

### `IN` Predicates

Evaluates set membership across literal lists and subqueries:

```sql
-- Literal list
SELECT * FROM products WHERE category IN ('Electronics', 'Hardware', 'Networking');

-- Subquery membership
SELECT * FROM orders WHERE customer_id IN (SELECT id FROM premium_customers);

-- Negative membership
SELECT * FROM employees WHERE dept_id NOT IN (10, 20, 30);
```

### `EXISTS` and `NOT EXISTS`

Short-circuit evaluation halts scanning on the very first matching record:

```sql
SELECT * FROM departments d
WHERE EXISTS (SELECT 1 FROM employees e WHERE e.dept_id = d.id);
```

### `IS NULL` and `IS NOT NULL`

Directly inspects the slotted page null bitmask in a single instruction per 64 columns:

```sql
SELECT * FROM users WHERE phone_number IS NOT NULL AND notes IS NULL;
```

### `BETWEEN` and `LIKE`

Continuous range bounds and wildcard pattern matching (`%`, `_`):

```sql
SELECT * FROM transactions WHERE amount BETWEEN 50.0 AND 500.0;
SELECT * FROM users WHERE email LIKE '%@capital-hedge.com';
```

---

## 3. Cache-Conscious Joins & Streaming Set Operations

### Outer Joins

Extends standard inner hash joins to preserve unmatched records from either or both relations:

```sql
-- LEFT JOIN: preserves all left records
SELECT e.name, d.dept_name
FROM employees e
LEFT JOIN departments d ON e.dept_id = d.dept_id;

-- RIGHT JOIN: preserves all right records
SELECT e.name, d.dept_name
FROM employees e
RIGHT JOIN departments d ON e.dept_id = d.dept_id;

-- FULL OUTER JOIN: preserves unmatched rows from both tables
SELECT e.name, d.dept_name
FROM employees e
FULL JOIN departments d ON e.dept_id = d.dept_id;

-- CROSS JOIN: Cartesian product of relations
SELECT colors.name, sizes.label FROM colors CROSS JOIN sizes;
```

### `USING` Syntax

Simplified join equality clause when both relations share identical column names:

```sql
SELECT employees.name, departments.dept_name
FROM employees
JOIN departments USING (dept_id);
```

### Streaming Set Operations

Multi-query combination with zero-copy iterator chaining:

```sql
-- UNION ALL (Preserves duplicates without materializing a set)
SELECT item FROM warehouse_a UNION ALL SELECT item FROM warehouse_b;

-- UNION (Deduplicated)
SELECT item FROM warehouse_a UNION SELECT item FROM warehouse_b;

-- INTERSECT (Set intersection)
SELECT item FROM warehouse_a INTERSECT SELECT item FROM warehouse_b;

-- EXCEPT (Set difference)
SELECT item FROM warehouse_a EXCEPT SELECT item FROM warehouse_b;
```

---

## 4. Extended DML, Atomic UPSERT & Pipelined RETURNING

### Direct-Stream `INSERT ... SELECT`

Streams rows directly into target storage blocks without intermediate Python or C++ allocations:

```sql
-- Direct migration
INSERT INTO archive_orders SELECT * FROM active_orders WHERE status = 'delivered';

-- Target column mapping
INSERT INTO order_summary (order_id, customer)
SELECT id, customer_name FROM orders;
```

### Atomic UPSERT

Idempotent insertion and conflict-driven slot modification via primary key index:

```sql
-- ON CONFLICT DO UPDATE SET (UPSERT)
INSERT INTO inventory (sku, name, quantity, price)
VALUES (101, 'NVMe SSD 2TB', 50, 149.99)
ON CONFLICT (sku) DO UPDATE SET
    quantity = EXCLUDED.quantity,
    price = EXCLUDED.price;

-- ON CONFLICT DO NOTHING (Safe idempotent insert)
INSERT INTO subscribers (email) VALUES ('user@example.com')
ON CONFLICT (email) DO NOTHING;
```

### Pipelined `RETURNING`

Intercepts mutation page records during the mutation pass and streams them directly into outbound results:

```sql
-- INSERT ... RETURNING
INSERT INTO accounts (name, balance) VALUES ('Alice', 1000.0)
RETURNING id, name, balance;

-- UPDATE ... RETURNING
UPDATE inventory SET quantity = 100 WHERE sku = 101
RETURNING sku, quantity;

-- DELETE ... RETURNING
DELETE FROM sessions WHERE expired_at < now()
RETURNING session_id, user_id;
```

---

## 5. Zero-Copy Schema Evolution, Views & Fast Indexing

### Zero-Downtime `ALTER TABLE`

Modifies table schema metadata without rewriting data pages on disk:

```sql
-- Add column with DEFAULT (Metadata-only evolution)
ALTER TABLE users ADD COLUMN IF NOT EXISTS role TEXT DEFAULT 'member';

-- Rename column (O(1) identifier swap)
ALTER TABLE users RENAME COLUMN role TO user_role;

-- Drop column (Soft deletion from catalog)
ALTER TABLE users DROP COLUMN IF EXISTS notes;

-- Rename table
ALTER TABLE users RENAME TO members;
```

### Virtual Views

Logical abstractions expanded in-place during query parsing without materialization overhead:

```sql
-- Create view
CREATE VIEW active_users AS
SELECT id, username, balance FROM members WHERE is_active = true;

-- Query view with additional predicates
SELECT * FROM active_users WHERE balance > 1000.0;

-- Replace view
CREATE OR REPLACE VIEW active_users AS
SELECT id, username, balance, user_role FROM members WHERE is_active = true;

-- Drop view
DROP VIEW IF EXISTS active_users;
```

### Secondary Indexes

High-performance adaptive B+ Tree indexes for 15-nanosecond point lookups:

```sql
CREATE INDEX idx_members_email ON members (email);
CREATE UNIQUE INDEX IF NOT EXISTS idx_members_user ON members (username);
DROP INDEX IF EXISTS idx_members_email;
```

---

## 6. Microsecond Savepoints & Nested Transactions

### `SAVEPOINT` Grammar

Sets lightweight transactional marks within an active transaction and rolls back partially:

```sql
BEGIN;
INSERT INTO accounts VALUES (1, 'Alice', 1000.0);
SAVEPOINT sp1;

INSERT INTO accounts VALUES (2, 'Bob', 500.0);
SAVEPOINT sp2;

-- Partial rollback: undos Bob, retains Alice
ROLLBACK TO SAVEPOINT sp1;

-- Release savepoint mark
RELEASE SAVEPOINT sp1;
COMMIT;
```

### Python API Integration

```python
with conn.transaction() as txn:
    conn.execute("INSERT INTO orders VALUES (101, 'Widget');")
    
    # Mark savepoint
    txn.savepoint("speculative_step")
    try:
        conn.execute("INSERT INTO orders VALUES (102, 'RiskyItem');")
        # Validate conditions...
        if error_condition:
            txn.rollback_to("speculative_step")
    finally:
        txn.release_savepoint("speculative_step")
```

