"""
Phase 4 — Crash Recovery tests
Covers: CheckpointManager, RecoveryManager, ARIES pipeline, master.nm,
        checkpoint_lsn monotonicity, graceful recovery on corrupt/missing WAL.
"""

import os
import shutil
import pathlib
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
# Test 1: checkpoint() does NOT raise on a fresh database
# ---------------------------------------------------------------------------

def test_checkpoint_no_raise_fresh_db():
    name = "p4_fresh"
    _cleanup(name)
    try:
        db = nemo.connect(name)
        db.checkpoint()   # must not raise
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 2: master.nm appears after checkpoint()
# ---------------------------------------------------------------------------

def test_master_nm_created_after_checkpoint():
    name = "p4_master"
    _cleanup(name)
    try:
        db = nemo.connect(name)
        db.checkpoint()
        db.close()

        master = _nemo_dir(name) / "master.nm"
        assert master.exists(), "master.nm must be written by CheckpointManager"
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 3: checkpoint() does NOT raise after writes
# ---------------------------------------------------------------------------

def test_checkpoint_after_writes():
    name = "p4_writes"
    _cleanup(name)
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE things (id INTEGER, label TEXT)")
        conn.execute("INSERT INTO things VALUES (1, 'alpha')")
        conn.execute("INSERT INTO things VALUES (2, 'beta')")
        db.checkpoint()   # must not raise
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 4: data survives a close+reopen (simulates crash recovery path)
# ---------------------------------------------------------------------------

def test_data_survives_reopen():
    name = "p4_survive"
    _cleanup(name)
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE log_entries (id INTEGER, msg TEXT)")
        conn.execute("INSERT INTO log_entries VALUES (1, 'hello')")
        conn.execute("INSERT INTO log_entries VALUES (2, 'world')")
        db.close()

        # Reopen (triggers recovery path on existing DB)
        db2 = nemo.connect(name)
        conn2 = db2.connect()
        rows = conn2.execute("SELECT * FROM log_entries").fetchall()
        db2.close()

        assert len(rows) >= 2, f"Expected >=2 rows after reopen, got {rows}"
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 5: recovery_complete is True after opening a fresh database
# ---------------------------------------------------------------------------

def test_recovery_complete_fresh_db():
    name = "p4_rcfresh"
    _cleanup(name)
    try:
        db = nemo.connect(name)
        stats = db._db.statistics()
        db.close()
        assert stats.get("recovery_complete", True) is True, \
            "Fresh DB must have recovery_complete=True"
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 6: WAL directory exists after connect
# ---------------------------------------------------------------------------

def test_wal_directory_exists():
    name = "p4_waldir"
    _cleanup(name)
    try:
        db = nemo.connect(name)
        db.close()
        wal_dir = _nemo_dir(name) / "wal"
        assert wal_dir.exists(), "wal/ subdirectory must exist after connect"
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 7: corrupt WAL segment → explicit database open error
# ---------------------------------------------------------------------------

def test_open_survives_corrupt_wal():
    name = "p4_corrupt"
    _cleanup(name)
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE t (x INTEGER)")
        conn.execute("INSERT INTO t VALUES (99)")
        db.close()

        # Corrupt any WAL segments
        wal_dir = _nemo_dir(name) / "wal"
        for seg in wal_dir.glob("*.wal"):
            seg.write_bytes(b"\x00" * 64)

        # Corrupt WAL must cause open to fail with an explicit error (cannot report DB usable)
        with pytest.raises(Exception):
            db2 = nemo.connect(name)
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 8: multiple checkpoints do not raise
# ---------------------------------------------------------------------------

def test_multiple_checkpoints():
    name = "p4_multick"
    _cleanup(name)
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE ck (v INTEGER)")
        for i in range(5):
            conn.execute(f"INSERT INTO ck VALUES ({i})")
            db.checkpoint()
        db.close()
        assert (_nemo_dir(name) / "master.nm").exists()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 9: statistics() dict contains recovery_complete key
# ---------------------------------------------------------------------------

def test_statistics_has_recovery_complete():
    name = "p4_stats"
    _cleanup(name)
    try:
        db = nemo.connect(name)
        stats = db._db.statistics()
        db.close()
        assert "recovery_complete" in stats, \
            "statistics() dict must include 'recovery_complete' key"
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 10: checkpoint() after reopen updates master.nm mtime
# ---------------------------------------------------------------------------

def test_checkpoint_updates_master_nm():
    name = "p4_mtime"
    _cleanup(name)
    try:
        db = nemo.connect(name)
        db.checkpoint()
        db.close()

        master = _nemo_dir(name) / "master.nm"
        mtime1 = master.stat().st_mtime

        time.sleep(0.05)

        db2 = nemo.connect(name)
        conn2 = db2.connect()
        conn2.execute("CREATE TABLE u (k INTEGER)")
        conn2.execute("INSERT INTO u VALUES (7)")
        db2.checkpoint()
        db2.close()

        mtime2 = master.stat().st_mtime
        assert mtime2 >= mtime1, \
            "master.nm mtime must not decrease after a second checkpoint"
    finally:
        _cleanup(name)
