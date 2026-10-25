# Nemo SQL Parameters & Prepared Statements Reference

Parameterized queries are the cornerstone of secure, high-performance database interactions in **Nemo**. They separate SQL query logic from user-supplied data, eliminating SQL injection vulnerabilities and allowing the C++ engine to optimize execution plans across repeated calls.

---

## Table of Contents

1. [Why Use Parameterized Queries?](#1-why-use-parameterized-queries)
2. [Parameter Placeholder Syntax](#2-parameter-placeholder-syntax)
3. [Python to Nemo Datatype Coercion](#3-python-to-nemo-datatype-coercion)
4. [Single Execution (`execute`)](#4-single-execution-execute)
5. [Bulk Ingestion (`executemany`)](#5-bulk-ingestion-executemany)
6. [Prepared Statements (`prepare`)](#6-prepared-statements-prepare)
7. [Automatic Parameter Redaction & Logging Security](#7-automatic-parameter-redaction--logging-security)
8. [Best Practices & Anti-Patterns](#8-best-practices--anti-patterns)

---

## 1. Why Use Parameterized Queries?

When executing SQL queries, you should **never format variables directly into SQL strings** (e.g. using `f"SELECT * FROM users WHERE name = '{user_input}'"`). 

### Security: SQL Injection Elimination
User input can contain malicious SQL fragments (e.g. `' OR 1=1 --`). In Nemo, parameterized values are transmitted out-of-band directly to the C++ type system without string concatenation, making SQL injection impossible.

### Performance: Plan Caching & Zero-Copy Marshaling
When queries use `?` placeholders or are pre-compiled via `conn.prepare()`, the Nemo query planner compiles the abstract syntax tree (AST) once. Subsequent executions bind binary parameters directly into slotted tuple buffers with zero unnecessary string allocations.

---

## 2. Parameter Placeholder Syntax

Nemo uses standard positional parameter placeholders represented by the question mark character (`?`):

```sql
SELECT * FROM accounts WHERE id = ? AND is_active = ?;
```

Parameters must be supplied as a Python sequence (tuple or list) matching the exact count of placeholders in the query:

```python
# Pass parameters as a tuple
result = conn.execute(
    "SELECT * FROM accounts WHERE id = ? AND is_active = ?",
    (101, True)
)
```

> [!NOTE]
> Single-parameter queries must pass a 1-element tuple with a trailing comma: `(101,)` or a list: `[101]`.

---

## 3. Python to Nemo Datatype Coercion

When Python values are passed to `execute()` or `executemany()`, Nemo automatically marshals them to the corresponding C++20 engine types:

| Python Object | Nemo Target Datatype | Description & Coercion Rules |
| :--- | :--- | :--- |
| `None` | `NULL` | Bound as an unassigned null slot. |
| `bool` (`True`, `False`) | `BOOLEAN` | Stored as a 1-byte boolean flag. |
| `int` | `SMALLINT`, `INTEGER`, `BIGINT` | Bound as 64-bit signed integer. Fit into 16-bit or 32-bit columns automatically. |
| `float` | `REAL`, `DOUBLE PRECISION` | Bound as 64-bit IEEE 754 floating point. |
| `decimal.Decimal` | `DECIMAL(p, s)` / `NUMERIC` | Exact fixed-point numeric representation without binary float rounding error. |
| `str` | `VARCHAR`, `TEXT`, `CHAR` | UTF-8 encoded text string. |
| `bytes`, `bytearray` | `BYTEA`, `BLOB` | Raw binary payload bytes. |
| `datetime.date` | `DATE` | Coerced to standard ISO date (`YYYY-MM-DD`). |
| `datetime.time` | `TIME` | Coerced to clock time (`HH:MM:SS`). |
| `datetime.datetime` | `TIMESTAMP` / `TIMESTAMPTZ` | Coerced to ISO 8601 string (`YYYY-MM-DDTHH:MM:SSZ`). |
| `datetime.timedelta` | `INTERVAL` | Formatted as timespan string (`X days HH:MM:SS`). |
| `uuid.UUID` | `UUID` | Stored as 128-bit unique identifier or formatted canonical 36-char string. |
| `list` | `ARRAY` | Marshaled as a native typed array collection. |
| `dict` | `JSON`, `JSONB`, `DICT` | Automatically serialized to JSON document or key-value dictionary. |

---

## 4. Single Execution (`execute`)

Use `conn.execute(sql, parameters)` for single transactional operations:

### Synchronous Usage
```python
import nemo

with nemo.connect("analytics") as db:
    with db.connect_sync() as conn:
        # Parameterized INSERT
        conn.execute(
            """
            INSERT INTO telemetry (device_id, cpu_load, tags, payload) 
            VALUES (?, ?, ?, ?)
            """,
            ("sensor_01", 84.5, ["rack_a", "core"], {"temp": 42.1, "fan_rpm": 2400})
        )

        # Parameterized SELECT
        res = conn.execute(
            "SELECT * FROM telemetry WHERE device_id = ? AND cpu_load > ?",
            ("sensor_01", 80.0)
        )
        row = res.fetchone()
        print("Record:", row)
```

### Asynchronous Usage
```python
import asyncio
import nemo

async def main():
    with nemo.connect("analytics") as db:
        async with db.connect_async() as conn:
            res = await conn.execute(
                "SELECT * FROM telemetry WHERE device_id = ?",
                ("sensor_01",)
            )
            row = await res.fetchone()
            print("Async Record:", row)

asyncio.run(main())
```

---

## 5. Bulk Ingestion (`executemany`)

For massive batch data insertion, use `executemany(sql, param_sequences)`. Nemo's vectorized ingestion pipeline bypasses per-statement parsing, achieving throughput exceeding **1,000,000 rows/second**.

```python
import nemo

records = [
    (i, f"user_{i}", round(10.5 * i, 2), ["standard", "v1"])
    for i in range(1, 50001)
]

with nemo.connect("benchmark") as db:
    with db.connect_sync() as conn:
        conn.execute("CREATE TABLE users (id BIGINT PRIMARY KEY, name TEXT, balance DOUBLE, tags ARRAY);")
        
        # Batch insert 50,000 records in a single vectorized call
        conn.executemany(
            "INSERT INTO users (id, name, balance, tags) VALUES (?, ?, ?, ?);",
            records
        )
        print("Inserted count:", conn.count("users"))
```

---

## 6. Prepared Statements (`prepare`)

When executing the same query repeatedly with different parameter bindings, `prepare()` compiles and validates the SQL statement once, returning a reusable `PreparedStatement` object.

```python
import nemo

with nemo.connect("production") as db:
    with db.connect_sync() as conn:
        # Pre-compile SQL statement
        stmt = conn.prepare("SELECT name, balance FROM users WHERE id = ?")

        # Execute multiple point lookups with minimal overhead
        for user_id in [10, 25, 42, 99]:
            result = stmt.execute((user_id,))
            print(f"User {user_id}:", result.fetchone())

        # Clean up prepared handle
        stmt.close()
```

Prepared statements also support batch execution:
```python
stmt = conn.prepare("INSERT INTO log_entries VALUES (?, ?, ?)")
stmt.executemany(batch_of_tuples)
stmt.close()
```

---

## 7. Automatic Parameter Redaction & Logging Security

When diagnostics or query logging are enabled via `nemo.configure_logging()`, Nemo includes an intelligent parameter redaction engine to prevent sensitive user information (passwords, tokens, credit cards) from leaking into log files or APM traces:

```python
import nemo

# Enable query logging with parameter redaction
nemo.configure_logging(
    level="DEBUG",
    log_slow_queries=True,
    slow_query_threshold_ms=10.0,
    redact_sensitive_params=True
)

with nemo.connect("secure_db") as db:
    with db.connect_sync() as conn:
        # In logs, parameters are automatically masked as [REDACTED]
        conn.execute(
            "INSERT INTO users (username, password_hash, ssn) VALUES (?, ?, ?)",
            ("alice", "argon2$secret_hash...", "123-45-6789")
        )
```

Log Output:
```text
[DEBUG] [Nemo:SQL] Executed: INSERT INTO users (username, password_hash, ssn) VALUES (?, ?, ?) | Parameters: ['alice', '[REDACTED]', '[REDACTED]'] (0.42 ms)
```

---

## 8. Best Practices & Anti-Patterns

### ❌ Anti-Patterns to Avoid

```python
# 1. DANGEROUS: String formatting (Vulnerable to SQL Injection)
conn.execute(f"SELECT * FROM users WHERE username = '{username}'")

# 2. SLOW: Stringifying native structures
conn.execute("INSERT INTO items VALUES (?, ?)", (1, str({"tier": 1})))  # Pass Python dict directly!

# 3. INEFFICIENT: Looping single inserts instead of executemany
for r in records:
    conn.execute("INSERT INTO items VALUES (?, ?)", r)  # Use conn.executemany()!
```

###  Recommended Best Practices

```python
# 1. Always pass parameters as tuple or list
conn.execute("SELECT * FROM users WHERE username = ?", (username,))

# 2. Pass native Python dicts and lists directly
conn.execute("INSERT INTO items VALUES (?, ?)", (1, {"tier": 1, "active": True}))

# 3. Use executemany for high-throughput batch writes
conn.executemany("INSERT INTO items VALUES (?, ?)", batch_of_items)

# 4. Use prepare() when executing repetitive parameterized point queries
stmt = conn.prepare("SELECT * FROM cache WHERE key = ?")
```

---

## Related Runnable Examples

- [`example_1.py`](../example/example_1.py): Positional Parameter Placeholders (`?`) and Basic Types.
- [`example_11.py`](../example/example_11.py): Parameterized Queries and Reusable Prepared Statements (`conn.prepare()`).
- [`example_31.py`](../example/example_31.py): Common Table Expressions (CTEs) with Parameter Bindings.
- [`example_32.py`](../example/example_32.py): Filter Expressions, Ranges, and Parameterized Point Lookups.


