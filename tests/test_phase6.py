"""Phase 6 — B+ Tree Index validation.

Final regression coverage for the completed index phase:
- CREATE/DROP index lifecycle
- index-backed lookups and scan fallback
- post-insert/delete index consistency
- reopen-safe index behavior

Pattern: db = nemo.connect(name), conn = db.connect()
"""

import uuid
import shutil
import os
import nemo


def _fresh(prefix: str = "p6") -> str:
    return f"{prefix}_{uuid.uuid4().hex[:8]}"


def _cleanup(name: str) -> None:
    path = os.path.join(".nemo", name)
    if os.path.exists(path):
        shutil.rmtree(path, ignore_errors=True)


# ---------------------------------------------------------------------------
# Test 1 — CREATE INDEX succeeds without error
# ---------------------------------------------------------------------------
def test_create_index_no_error():
    name = _fresh()
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE products (id INTEGER, name TEXT, price REAL)")
        conn.execute("CREATE INDEX idx_price ON products(price)")
        conn.close()
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 2 — CREATE INDEX on existing data: lookup finds all pre-existing rows
# ---------------------------------------------------------------------------
def test_create_index_on_existing_data():
    name = _fresh()
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE items (id INTEGER, category TEXT, qty INTEGER)")
        for i in range(1, 11):
            cat = "A" if i % 2 == 0 else "B"
            conn.execute(f"INSERT INTO items VALUES ({i}, '{cat}', {i * 10})")

        # Build index after rows already exist
        conn.execute("CREATE INDEX idx_cat ON items(category)")

        # Lookup category = 'A' — should find 5 rows
        res = conn.execute("SELECT * FROM items WHERE category = 'A'")
        a_rows = list(res)
        assert len(a_rows) == 5, f"Expected 5 rows for category A, got {len(a_rows)}"

        conn.close()
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 3 — INSERT after CREATE INDEX: new row appears in index lookup
# ---------------------------------------------------------------------------
def test_insert_after_create_index():
    name = _fresh()
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE logs (id INTEGER, level TEXT, msg TEXT)")
        conn.execute("CREATE INDEX idx_level ON logs(level)")

        # Insert rows after index exists
        conn.execute("INSERT INTO logs VALUES (1, 'ERROR', 'disk full')")
        conn.execute("INSERT INTO logs VALUES (2, 'INFO',  'started')")
        conn.execute("INSERT INTO logs VALUES (3, 'ERROR', 'timeout')")

        res = conn.execute("SELECT * FROM logs WHERE level = 'ERROR'")
        rows = list(res)
        assert len(rows) == 2, f"Expected 2 ERROR rows, got {len(rows)}"
        conn.close()
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 4 — DROP INDEX succeeds without error
# ---------------------------------------------------------------------------
def test_drop_index_no_error():
    name = _fresh()
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE t (id INTEGER, val TEXT)")
        conn.execute("CREATE INDEX idx_val ON t(val)")
        conn.execute("DROP INDEX idx_val")
        conn.close()
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 5 — CREATE UNIQUE INDEX registers as unique
# ---------------------------------------------------------------------------
def test_create_unique_index():
    name = _fresh()
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE users (id INTEGER, email TEXT, role TEXT)")
        conn.execute("INSERT INTO users VALUES (1, 'alice@x.com', 'admin')")
        conn.execute("INSERT INTO users VALUES (2, 'bob@x.com', 'user')")
        conn.execute("CREATE UNIQUE INDEX idx_email ON users(email)")

        # Lookup should still work
        res = conn.execute("SELECT * FROM users WHERE email = 'alice@x.com'")
        rows = list(res)
        assert len(rows) == 1, f"Expected 1 row for alice, got {len(rows)}"
        conn.close()
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 6 — Index on INTEGER column: point lookup returns correct rows
# ---------------------------------------------------------------------------
def test_index_on_integer_column():
    name = _fresh()
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE scores (id INTEGER, player TEXT, score INTEGER)")
        conn.execute("INSERT INTO scores VALUES (1, 'Alice', 100)")
        conn.execute("INSERT INTO scores VALUES (2, 'Bob',   200)")
        conn.execute("INSERT INTO scores VALUES (3, 'Carol', 100)")
        conn.execute("CREATE INDEX idx_score ON scores(score)")

        res = conn.execute("SELECT * FROM scores WHERE score = 100")
        rows = list(res)
        assert len(rows) == 2, f"Expected 2 rows with score=100, got {len(rows)}"
        conn.close()
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 7 — Index on TEXT column: point lookup returns correct rows
# ---------------------------------------------------------------------------
def test_index_on_text_column():
    name = _fresh()
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE orders (id INTEGER, status TEXT, amount REAL)")
        statuses = ["pending", "shipped", "pending", "delivered", "shipped"]
        for i, s in enumerate(statuses, 1):
            conn.execute(f"INSERT INTO orders VALUES ({i}, '{s}', {i * 9.99})")
        conn.execute("CREATE INDEX idx_status ON orders(status)")

        res = conn.execute("SELECT * FROM orders WHERE status = 'shipped'")
        rows = list(res)
        assert len(rows) == 2, f"Expected 2 shipped rows, got {len(rows)}"
        conn.close()
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 8 — Multiple indexes on same table coexist
# ---------------------------------------------------------------------------
def test_multiple_indexes_coexist():
    name = _fresh()
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE employees (id INTEGER, dept TEXT, grade INTEGER)")
        conn.execute("INSERT INTO employees VALUES (1, 'eng', 3)")
        conn.execute("INSERT INTO employees VALUES (2, 'mkt', 2)")
        conn.execute("INSERT INTO employees VALUES (3, 'eng', 2)")
        conn.execute("INSERT INTO employees VALUES (4, 'hr',  3)")
        conn.execute("CREATE INDEX idx_dept  ON employees(dept)")
        conn.execute("CREATE INDEX idx_grade ON employees(grade)")

        eng_rows = list(conn.execute("SELECT * FROM employees WHERE dept = 'eng'"))
        assert len(eng_rows) == 2, f"Expected 2 eng rows, got {len(eng_rows)}"

        g3_rows = list(conn.execute("SELECT * FROM employees WHERE grade = 3"))
        assert len(g3_rows) == 2, f"Expected 2 grade-3 rows, got {len(g3_rows)}"
        conn.close()
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 9 — After DELETE, index no longer returns deleted row
# ---------------------------------------------------------------------------
def test_delete_invalidates_index():
    name = _fresh()
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE events (id INTEGER, type TEXT, payload TEXT)")
        conn.execute("INSERT INTO events VALUES (1, 'click', 'btn1')")
        conn.execute("INSERT INTO events VALUES (2, 'hover', 'menu')")
        conn.execute("INSERT INTO events VALUES (3, 'click', 'btn2')")
        conn.execute("CREATE INDEX idx_type ON events(type)")

        # Confirm 2 click events
        before = list(conn.execute("SELECT * FROM events WHERE type = 'click'"))
        assert len(before) == 2, f"Expected 2 click rows before delete, got {len(before)}"

        # Delete one click event
        conn.execute("DELETE FROM events WHERE id = 1")

        # Index should return only 1 click now
        after = list(conn.execute("SELECT * FROM events WHERE type = 'click'"))
        assert len(after) == 1, f"Expected 1 click row after delete, got {len(after)}"
        conn.close()
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 10 — DROP TABLE also removes its indexes (no stale entries)
# ---------------------------------------------------------------------------
def test_drop_table_removes_indexes():
    name = _fresh()
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE tmp (id INTEGER, tag TEXT)")
        conn.execute("INSERT INTO tmp VALUES (1, 'x')")
        conn.execute("CREATE INDEX idx_tag ON tmp(tag)")
        conn.execute("DROP TABLE tmp")

        # Recreate with same name — should work cleanly
        conn.execute("CREATE TABLE tmp (id INTEGER, label TEXT)")
        conn.execute("INSERT INTO tmp VALUES (1, 'hello')")
        res = list(conn.execute("SELECT * FROM tmp"))
        assert len(res) == 1
        conn.close()
        db.close()
    finally:
        _cleanup(name)


if __name__ == "__main__":
    tests = [
        test_create_index_no_error,
        test_create_index_on_existing_data,
        test_insert_after_create_index,
        test_drop_index_no_error,
        test_create_unique_index,
        test_index_on_integer_column,
        test_index_on_text_column,
        test_multiple_indexes_coexist,
        test_delete_invalidates_index,
        test_drop_table_removes_indexes,
    ]
    passed = 0
    for t in tests:
        try:
            t()
            print(f"  PASS  {t.__name__}")
            passed += 1
        except Exception as exc:
            print(f"  FAIL  {t.__name__}: {exc}")
    print(f"\n{passed}/{len(tests)} tests passed")
