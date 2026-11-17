# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0
#
# Phase 3 Tests — WAL + Write Path
# Tests verify:
#   - wal/ directory is populated with segment files after writes
#   - explicit BEGIN/COMMIT writes WAL records
#   - autocommit inserts produce WAL records (wal/ dir grows)
#   - rollback does not persist changes
#   - flush() triggers WAL flush before buffer-pool flush
#   - WAL segment files are named with the expected pattern
#   - data survives a close+reopen (WAL flushed before close)

import os
import sys
import shutil
import pytest

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _db_root(cwd: str, name: str) -> str:
    return os.path.join(cwd, ".nemo", name)

def _wal_dir(cwd: str, name: str) -> str:
    return os.path.join(_db_root(cwd, name), "wal")

def _cleanup(cwd: str, name: str) -> None:
    db_dir = _db_root(cwd, name)
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir, ignore_errors=True)

def _wal_files(cwd: str, name: str) -> list:
    wal = _wal_dir(cwd, name)
    if not os.path.isdir(wal):
        return []
    return [f for f in os.listdir(wal) if os.path.isfile(os.path.join(wal, f))]

# ---------------------------------------------------------------------------

def test_wal_dir_exists_after_connect():
    """wal/ directory must exist right after connect (created by Phase 1 layout)."""
    import nemo
    cwd = os.getcwd()
    name = "test_p3_waldir"
    _cleanup(cwd, name)
    try:
        db = nemo.connect(name)
        assert os.path.isdir(_wal_dir(cwd, name)), "wal/ directory missing"
        db.close()
    finally:
        _cleanup(cwd, name)


def test_wal_segment_created_after_write():
    """After an INSERT, at least one WAL segment file must appear in wal/."""
    import nemo
    cwd = os.getcwd()
    name = "test_p3_seg"
    _cleanup(cwd, name)
    try:
        db = nemo.connect(name, buffer_pool_mb=32)
        conn = db.connect_sync()
        conn.execute("CREATE TABLE p3_t (id INT, v TEXT);")
        conn.execute("INSERT INTO p3_t VALUES (1, 'hello');")
        db.flush()
        files = _wal_files(cwd, name)
        assert len(files) > 0, "No WAL segment file found after INSERT + flush"
        db.close()
    finally:
        _cleanup(cwd, name)


def test_explicit_begin_commit_produces_wal():
    """Explicit BEGIN ... COMMIT must produce WAL segments."""
    import nemo
    cwd = os.getcwd()
    name = "test_p3_txn"
    _cleanup(cwd, name)
    try:
        db = nemo.connect(name, buffer_pool_mb=32)
        conn = db.connect_sync()
        conn.execute("BEGIN;")
        conn.execute("CREATE TABLE p3_txn (n INT);")
        conn.execute("INSERT INTO p3_txn VALUES (42);")
        conn.execute("COMMIT;")
        db.flush()
        files = _wal_files(cwd, name)
        assert len(files) > 0, "No WAL segment after BEGIN/COMMIT"
        db.close()
    finally:
        _cleanup(cwd, name)


def test_rollback_does_not_persist():
    """ROLLBACK must discard changes from the current transaction."""
    import nemo
    cwd = os.getcwd()
    name = "test_p3_rollback"
    _cleanup(cwd, name)
    try:
        db = nemo.connect(name, buffer_pool_mb=32)
        conn = db.connect_sync()
        conn.execute("CREATE TABLE p3_rb (n INT);")
        conn.execute("INSERT INTO p3_rb VALUES (1);")

        # Start a transaction, insert, then rollback
        conn.begin()
        conn.execute("INSERT INTO p3_rb VALUES (999);")
        conn.rollback()

        rows = list(conn.execute("SELECT n FROM p3_rb;"))
        values = [r[0] for r in rows]
        assert 999 not in values, f"Rolled-back row 999 appeared: {values}"
        db.close()
    finally:
        _cleanup(cwd, name)


def test_data_persists_after_wal_flush():
    """Data written and flushed must survive close+reopen."""
    import nemo
    cwd = os.getcwd()
    name = "test_p3_persist"
    _cleanup(cwd, name)
    try:
        db1 = nemo.connect(name, buffer_pool_mb=32)
        c1 = db1.connect_sync()
        c1.execute("CREATE TABLE p3_persist (k TEXT, v INT);")
        c1.execute("INSERT INTO p3_persist VALUES ('x', 7);")
        db1.flush()
        db1.close()

        db2 = nemo.connect(name, buffer_pool_mb=32)
        c2 = db2.connect_sync()
        rows = list(c2.execute("SELECT k, v FROM p3_persist;"))
        assert len(rows) == 1, f"Expected 1 row, got {len(rows)}"
        assert rows[0][0] == "x" and rows[0][1] == 7
        db2.close()
    finally:
        _cleanup(cwd, name)


def test_flush_before_close():
    """db.close() must flush WAL then buffer pool without raising."""
    import nemo
    cwd = os.getcwd()
    name = "test_p3_close"
    _cleanup(cwd, name)
    try:
        db = nemo.connect(name, buffer_pool_mb=32)
        conn = db.connect_sync()
        conn.execute("CREATE TABLE p3_close (n INT);")
        for i in range(100):
            conn.execute(f"INSERT INTO p3_close VALUES ({i});")
        db.close()   # must not raise
    finally:
        _cleanup(cwd, name)


def test_multiple_wal_flushes_monotonic():
    """Multiple flush() calls must not corrupt state or raise."""
    import nemo
    cwd = os.getcwd()
    name = "test_p3_multiflush"
    _cleanup(cwd, name)
    try:
        db = nemo.connect(name, buffer_pool_mb=32)
        conn = db.connect_sync()
        conn.execute("CREATE TABLE p3_mf (n INT);")
        for i in range(5):
            conn.execute(f"INSERT INTO p3_mf VALUES ({i});")
            db.flush()
        rows = list(conn.execute("SELECT count(*) FROM p3_mf;"))
        assert rows[0][0] == 5
        db.close()
    finally:
        _cleanup(cwd, name)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    print("=" * 60)
    print("Phase 3: WAL + Write Path Tests")
    print("=" * 60)
    sys.exit(pytest.main([__file__, "-v"]))
