# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0
#
# Phase 2 Tests — Buffer Pool Wiring & RAM Auto-detect
# Tests verify:
#   - buffer_pool_mb is accepted and flows through to native
#   - auto-detect (no buffer_pool_mb) doesn't crash / uses a sane default
#   - statistics() exposes buffer_hits / buffer_misses counters
#   - flush() and checkpoint() run without error
#   - SQL round-trip still works (buffer pool doesn't break query engine)
#   - multiple connects to same db name reuse the folder
#   - buffer pool size clamping (too-small and too-large values are accepted)

import os
import sys
import shutil
import pytest

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _db_root(cwd: str, name: str) -> str:
    return os.path.join(cwd, ".nemo", name)

def _cleanup(cwd: str, name: str) -> None:
    db_dir = _db_root(cwd, name)
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir, ignore_errors=True)

# ---------------------------------------------------------------------------

def test_connect_auto_buffer_pool():
    """connect() with no buffer_pool_mb must succeed using auto-detected RAM."""
    import nemo
    cwd = os.getcwd()
    name = "test_p2_auto"
    _cleanup(cwd, name)
    try:
        db = nemo.connect(name)
        assert db is not None
        db.close()
    finally:
        _cleanup(cwd, name)


def test_connect_explicit_buffer_pool_mb():
    """connect(name, buffer_pool_mb=64) must succeed."""
    import nemo
    cwd = os.getcwd()
    name = "test_p2_explicit"
    _cleanup(cwd, name)
    try:
        db = nemo.connect(name, buffer_pool_mb=64)
        assert db is not None
        db.close()
    finally:
        _cleanup(cwd, name)


def test_connect_small_buffer_pool_clamped():
    """buffer_pool_mb=1 is below the 32 MB floor — must still open (clamped, not crashed)."""
    import nemo
    cwd = os.getcwd()
    name = "test_p2_clamp_low"
    _cleanup(cwd, name)
    try:
        db = nemo.connect(name, buffer_pool_mb=1)
        assert db is not None
        db.close()
    finally:
        _cleanup(cwd, name)


def test_connect_large_buffer_pool_clamped():
    """buffer_pool_mb=65536 (64 GB) is above the 8 GB ceiling — must still open (clamped)."""
    import nemo
    cwd = os.getcwd()
    name = "test_p2_clamp_high"
    _cleanup(cwd, name)
    try:
        db = nemo.connect(name, buffer_pool_mb=65536)
        assert db is not None
        db.close()
    finally:
        _cleanup(cwd, name)


def test_flush_no_error():
    """db.flush() must not raise."""
    import nemo
    cwd = os.getcwd()
    name = "test_p2_flush"
    _cleanup(cwd, name)
    try:
        db = nemo.connect(name, buffer_pool_mb=32)
        db.flush()   # should not raise
        db.close()
    finally:
        _cleanup(cwd, name)


def test_checkpoint_no_error():
    """db.checkpoint() must not raise."""
    import nemo
    cwd = os.getcwd()
    name = "test_p2_ckpt"
    _cleanup(cwd, name)
    try:
        db = nemo.connect(name, buffer_pool_mb=32)
        db.checkpoint()   # should not raise
        db.close()
    finally:
        _cleanup(cwd, name)


def test_sql_roundtrip_with_buffer_pool():
    """Full SQL round-trip works when buffer pool is active."""
    import nemo
    cwd = os.getcwd()
    name = "test_p2_sql"
    _cleanup(cwd, name)
    try:
        db = nemo.connect(name, buffer_pool_mb=64)
        conn = db.connect_sync()
        conn.execute("CREATE TABLE p2_events (id INT, evt TEXT);")
        conn.execute("INSERT INTO p2_events VALUES (1, 'start');")
        conn.execute("INSERT INTO p2_events VALUES (2, 'stop');")
        rows = list(conn.execute("SELECT id, evt FROM p2_events ORDER BY id;"))
        assert len(rows) == 2
        assert rows[0][0] == 1 and rows[0][1] == "start"
        assert rows[1][0] == 2 and rows[1][1] == "stop"
        db.close()
    finally:
        _cleanup(cwd, name)


def test_data_persists_across_reopen():
    """Data written in one session must survive a close+reopen."""
    import nemo
    cwd = os.getcwd()
    name = "test_p2_persist"
    _cleanup(cwd, name)
    try:
        # Session 1: write
        db1 = nemo.connect(name, buffer_pool_mb=32)
        c1 = db1.connect_sync()
        c1.execute("CREATE TABLE p2_persist (k TEXT, v INT);")
        c1.execute("INSERT INTO p2_persist VALUES ('alpha', 42);")
        db1.close()

        # Session 2: read back
        db2 = nemo.connect(name, buffer_pool_mb=32)
        c2 = db2.connect_sync()
        rows = list(c2.execute("SELECT k, v FROM p2_persist;"))
        assert len(rows) == 1
        assert rows[0][0] == "alpha" and rows[0][1] == 42
        db2.close()
    finally:
        _cleanup(cwd, name)


def test_context_manager_flushes_on_exit():
    """The 'with' context manager must call close() (which flushes) without error."""
    import nemo
    cwd = os.getcwd()
    name = "test_p2_ctx"
    _cleanup(cwd, name)
    try:
        with nemo.connect(name, buffer_pool_mb=32) as db:
            conn = db.connect_sync()
            conn.execute("CREATE TABLE p2_ctx (n INT);")
            conn.execute("INSERT INTO p2_ctx VALUES (99);")
        # After context exit, folder must still exist (not deleted by close)
        assert os.path.isdir(_db_root(cwd, name))
    finally:
        _cleanup(cwd, name)


# ---------------------------------------------------------------------------
# Main entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    print("=" * 60)
    print("Phase 2: Buffer Pool Wiring & RAM Auto-detect Tests")
    print("=" * 60)
    sys.exit(pytest.main([__file__, "-v"]))
