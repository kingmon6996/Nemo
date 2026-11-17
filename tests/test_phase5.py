"""
Phase 5 — Safe Concurrency validation
Final regression suite for the completed concurrency work:
- row-visibility filtering for committed vs. rolled-back rows
- per-table transaction tracking and rollback/commit semantics
- process lock file behavior and concurrent reader/writer safety
"""

import os
import shutil
import pathlib
import threading
import time

import pytest

NEMO_DIR = pathlib.Path.cwd() / ".nemo"


def _nemo_dir(name: str) -> pathlib.Path:
    return NEMO_DIR / name


def _cleanup(name: str) -> None:
    p = _nemo_dir(name)
    if p.exists():
        shutil.rmtree(p, ignore_errors=True)


try:
    import nemo
except ImportError:
    pytest.skip("nemo not installed – run after build", allow_module_level=True)


# ---------------------------------------------------------------------------
# Test 1: advisory lock file (.nemo/<name>/lock) is created on open
# ---------------------------------------------------------------------------

def test_lock_file_created_on_open():
    name = "p5_lockfile"
    _cleanup(name)
    try:
        db = nemo.connect(name)
        lock = _nemo_dir(name) / "lock"
        assert lock.exists(), "lock file must exist while the database is open"
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 2: lock file is removed after close()
# ---------------------------------------------------------------------------

def test_lock_file_removed_on_close():
    name = "p5_lockclose"
    _cleanup(name)
    try:
        db = nemo.connect(name)
        db.close()
        lock = _nemo_dir(name) / "lock"
        assert not lock.exists(), "lock file must be removed after close()"
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 3: concurrent reads from multiple connections — no deadlock
# ---------------------------------------------------------------------------

def test_concurrent_reads_no_deadlock():
    name = "p5_concread"
    _cleanup(name)
    errors = []
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE nums (n INTEGER)")
        for i in range(100):
            conn.execute(f"INSERT INTO nums VALUES ({i})")

        def reader():
            try:
                c = db.connect()
                rows = c.execute("SELECT * FROM nums").fetchall()
                assert len(rows) == 100
            except Exception as e:
                errors.append(e)

        threads = [threading.Thread(target=reader) for _ in range(8)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=10)

        db.close()
        assert not errors, f"Concurrent readers failed: {errors}"
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 4: concurrent inserts from multiple connections — no data loss, no crash
# ---------------------------------------------------------------------------

def test_concurrent_inserts_no_crash():
    name = "p5_concwrite"
    _cleanup(name)
    errors = []
    try:
        db = nemo.connect(name)
        setup = db.connect()
        setup.execute("CREATE TABLE events (id INTEGER, label TEXT)")

        insert_count = 20
        thread_count = 4

        def writer(tid: int):
            try:
                c = db.connect()
                for i in range(insert_count):
                    c.execute(f"INSERT INTO events VALUES ({tid * 100 + i}, 'w{tid}')")
            except Exception as e:
                errors.append(e)

        threads = [threading.Thread(target=writer, args=(t,)) for t in range(thread_count)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=30)

        # All inserts must be committed (auto-commit outside explicit txn)
        reader = db.connect()
        rows = reader.execute("SELECT * FROM events").fetchall()
        db.close()

        assert not errors, f"Concurrent writers raised: {errors}"
        assert len(rows) == thread_count * insert_count, \
            f"Expected {thread_count * insert_count} rows, got {len(rows)}"
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 5: Read-Committed — rollback hides rows from other readers
# ---------------------------------------------------------------------------

def test_read_committed_rollback_invisible():
    name = "p5_rcrollback"
    _cleanup(name)
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE rc_test (val INTEGER)")
        conn.execute("INSERT INTO rc_test VALUES (1)")   # committed (auto-commit)

        # Begin explicit txn on a second connection, insert but DO NOT commit
        writer = db.connect()
        txn = writer.transaction()
        txn.begin()
        writer.execute("INSERT INTO rc_test VALUES (999)")  # pending, not committed

        # A third reader must NOT see the uncommitted row
        reader = db.connect()
        rows = reader.execute("SELECT val FROM rc_test").fetchall()
        vals = [r[0] for r in rows]

        txn.rollback()   # discard the pending row

        # After rollback, reader also must not see it
        rows2 = reader.execute("SELECT val FROM rc_test").fetchall()
        vals2 = [r[0] for r in rows2]

        db.close()

        assert 999 not in vals, \
            f"Read-Committed: uncommitted row 999 must not be visible. Got: {vals}"
        assert 999 not in vals2, \
            f"After rollback: row 999 must not be visible. Got: {vals2}"
        assert 1 in vals, f"Committed row 1 must be visible. Got: {vals}"
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 6: Read-Committed — commit makes rows visible immediately
# ---------------------------------------------------------------------------

def test_read_committed_commit_visible():
    name = "p5_rccommit"
    _cleanup(name)
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE vis (val INTEGER)")

        writer = db.connect()
        txn = writer.transaction()
        txn.begin()
        writer.execute("INSERT INTO vis VALUES (42)")
        txn.commit()   # now committed

        reader = db.connect()
        rows = reader.execute("SELECT val FROM vis").fetchall()
        vals = [r[0] for r in rows]

        db.close()

        assert 42 in vals, f"Committed row 42 must be visible after commit. Got: {vals}"
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 7: mixed readers and writers simultaneously — no errors
# ---------------------------------------------------------------------------

def test_mixed_readers_writers():
    name = "p5_mixed"
    _cleanup(name)
    errors = []
    try:
        db = nemo.connect(name)
        setup = db.connect()
        setup.execute("CREATE TABLE mix (n INTEGER)")

        stop = threading.Event()

        def writer_fn():
            try:
                c = db.connect()
                for i in range(50):
                    c.execute(f"INSERT INTO mix VALUES ({i})")
                    time.sleep(0.001)
            except Exception as e:
                errors.append(("writer", e))

        def reader_fn():
            try:
                c = db.connect()
                for _ in range(20):
                    c.execute("SELECT * FROM mix").fetchall()
                    time.sleep(0.002)
            except Exception as e:
                errors.append(("reader", e))

        threads = (
            [threading.Thread(target=writer_fn) for _ in range(2)] +
            [threading.Thread(target=reader_fn) for _ in range(4)]
        )
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=30)

        db.close()
        assert not errors, f"Mixed workload errors: {errors}"
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 8: auto-commit inserts are immediately visible
# ---------------------------------------------------------------------------

def test_autocommit_rows_visible():
    name = "p5_autocommit"
    _cleanup(name)
    try:
        db = nemo.connect(name)
        w = db.connect()
        r = db.connect()

        w.execute("CREATE TABLE ac (v INTEGER)")
        w.execute("INSERT INTO ac VALUES (7)")   # auto-commit

        rows = r.execute("SELECT v FROM ac").fetchall()
        db.close()

        assert len(rows) == 1 and rows[0][0] == 7, \
            f"Auto-committed row must be immediately visible. Got: {rows}"
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 9: data persists across close+reopen without uncommitted rows
# ---------------------------------------------------------------------------

def test_no_uncommitted_rows_persisted():
    name = "p5_persist"
    _cleanup(name)
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE p (v INTEGER)")
        conn.execute("INSERT INTO p VALUES (1)")      # auto-commit → persisted

        # Begin txn, insert, then rollback (should NOT persist)
        txn = conn.transaction()
        txn.begin()
        conn.execute("INSERT INTO p VALUES (999)")
        txn.rollback()

        db.close()

        # Reopen and check
        db2 = nemo.connect(name)
        conn2 = db2.connect()
        rows = conn2.execute("SELECT v FROM p").fetchall()
        db2.close()

        vals = [r[0] for r in rows]
        assert 1 in vals, f"Committed row 1 must survive reopen. Got: {vals}"
        assert 999 not in vals, f"Rolled-back row 999 must NOT survive reopen. Got: {vals}"
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 10: lock file contains PID of the opening process
# ---------------------------------------------------------------------------

def test_lock_file_contains_pid():
    name = "p5_lockpid"
    _cleanup(name)
    try:
        db = nemo.connect(name)
        lock = _nemo_dir(name) / "lock"
        content = lock.read_text().strip()
        db.close()
        assert content == str(os.getpid()), \
            f"lock file must contain the current PID. Got: '{content}'"
    finally:
        _cleanup(name)
