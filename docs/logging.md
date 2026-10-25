# Logging, Diagnostics, and Exception Hierarchy

This document covers logging configuration, performance diagnostics, query profiling callbacks, and the complete exception hierarchy in Nemo.

---

## 1. Logging Configuration

Nemo includes a diagnostic logging subsystem built on top of standard Python `logging`.

### Quick Debug Enable

To quickly route internal database logs to `stdout` at `DEBUG` level:

```python
import nemo

nemo.enable_debug()
```

### Advanced `LoggingConfig`

For production environments, configure log levels, handlers, slow query thresholds, and parameter redaction using `LoggingConfig`:

```python
import nemo
from nemo import LoggingConfig, configure_logging

config = LoggingConfig(
    level="INFO",                        # "DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"
    slow_query_threshold_ms=50.0,        # Log queries exceeding 50ms as warnings
    redact_sensitive_data=True,          # Masks parameters in log output for security
    propagate_to_root=False              # Prevents double-logging to root logger
)

configure_logging(config)
```

### Logging Configuration Parameters

| Parameter | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `level` | `str` | `"WARNING"` | Minimum logging severity level. |
| `slow_query_threshold_ms` | `float` | `100.0` | Queries taking longer than this duration emit a performance warning. |
| `redact_sensitive_data` | `bool` | `True` | Redacts bound parameter values from query logs to prevent secret leakage. |
| `propagate_to_root` | `bool` | `False` | Whether events propagate up to the root Python logger. |

---

## 2. Exception Hierarchy

All exceptions raised by Nemo inherit from the base `NemoError` class.

```text
NemoError (Base exception)
 ├── DatabaseError (General database runtime failures)
 │    ├── OperationalError (IO, disk, connection, or storage engine faults)
 │    ├── IntegrityError (Unique constraint, primary key, or null violation)
 │    ├── ProgrammingError (Syntax errors, invalid SQL, unmapped types)
 │    ├── TransactionError (Invalid transaction state, double commit, or abort)
 │    └── ConcurrencyError (Transient conflict, lock collision, or serialization failure)
 └── TimeoutError (Query or connection acquisition timeout exceeded)
```

### Exception Reference

| Exception | Inherits From | Common Causes |
| :--- | :--- | :--- |
| `NemoError` | `Exception` | Base class for all Nemo exceptions. Catch this to intercept any Nemo error. |
| `DatabaseError` | `NemoError` | General database engine errors. |
| `OperationalError` | `DatabaseError` | Filesystem permission errors, corrupted header, disk full, or write failure. |
| `IntegrityError` | `DatabaseError` | Inserting duplicate values into a `PRIMARY KEY` or `UNIQUE` column, or `NULL` into a `NOT NULL` column. |
| `ProgrammingError` | `DatabaseError` | SQL syntax error, referencing a table or column that does not exist. |
| `TransactionError` | `DatabaseError` | Attempting to `commit()` or `rollback()` without an active transaction, or invalid transaction lifecycle. |
| `ConcurrencyError` | `DatabaseError` | Concurrent transaction conflict detected under high write load. |
| `TimeoutError` | `NemoError` | Statement execution or connection lease exceeded the configured timeout limit. |

---

## 3. Exception Handling Patterns

### Handling Constraint Violations

```python
import nemo
from nemo import IntegrityError

with nemo.connect("production") as db:
    with db.connect_sync() as conn:
        conn.execute("CREATE TABLE users (id INTEGER PRIMARY KEY, email VARCHAR UNIQUE);")
        conn.execute("INSERT INTO users VALUES (1, 'alice@example.com');")
        
        try:
            # Duplicate ID or email triggers IntegrityError
            conn.execute("INSERT INTO users VALUES (1, 'bob@example.com');")
        except IntegrityError as e:
            print(f"Integrity violation caught: {e}")
```

### Handling Concurrency Conflicts with Retries

```python
import time
import nemo
from nemo import ConcurrencyError

def update_counter(conn, user_id: int):
    for attempt in range(3):
        try:
            with conn.transaction():
                conn.execute("UPDATE users SET score = score + 1 WHERE id = ?;", (user_id,))
                return
        except ConcurrencyError:
            if attempt == 2:
                raise
            time.sleep(0.02 * (2 ** attempt))
```

### Handling Query Timeouts

```python
import nemo
from nemo import TimeoutError

with nemo.connect("analytics") as db:
    with db.connect_sync() as conn:
        try:
            # Execute with a 1.5-second timeout
            conn.execute("SELECT * FROM large_table;", timeout=1.5)
        except TimeoutError:
            print("Query exceeded the 1.5-second execution limit.")
```

---

## Related Runnable Examples

- [`example_12.py`](../example/example_12.py): Query Statistics, Buffer Cache Hits, and Performance Diagnostics.
- [`example_30.py`](../example/example_30.py): Cost-Based Optimizer (CBO) Plan Inspection and `EXPLAIN`.
- [`example_38.py`](../example/example_38.py): Pushdown Query Extraction and Query Monitoring.


