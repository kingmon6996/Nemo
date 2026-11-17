# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0
#
# Phase 1 Tests — Storage Layout, Name Validation, and Connect API
# Tests verify:
#   - .nemo/<name>/ folder layout is created correctly
#   - data.nm file is created inside the db folder
#   - wal/ subdirectory is created inside the db folder
#   - lock file path resolves correctly
#   - Name validation rejects dots, extensions, path separators, :memory:, empty
#   - connect(name) and connect(name, buffer_pool_mb=N) work
#   - path= parameter is NOT accepted
#   - Re-opening an existing database works
#   - Basic SQL round-trip works after connect

import os
import sys
import shutil
import pytest

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _nemo_root(cwd: str) -> str:
    return os.path.join(cwd, ".nemo")

def _db_root(cwd: str, name: str) -> str:
    return os.path.join(cwd, ".nemo", name)

def _cleanup(cwd: str, name: str) -> None:
    """Remove .nemo/<name>/ so each test starts clean."""
    db_dir = _db_root(cwd, name)
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir, ignore_errors=True)

# ---------------------------------------------------------------------------

def test_folder_layout_created():
    """connect() must create .nemo/<name>/ with data.nm and wal/ inside."""
    import nemo
    cwd = os.getcwd()
    name = "test_p1_layout"
    _cleanup(cwd, name)
    try:
        db = nemo.connect(name)
        db_dir = _db_root(cwd, name)
        assert os.path.isdir(db_dir), f".nemo/{name}/ not created"
        assert os.path.isfile(os.path.join(db_dir, "data.nm")), "data.nm missing"
        assert os.path.isdir(os.path.join(db_dir, "wal")), "wal/ missing"
        db.close()
    finally:
        _cleanup(cwd, name)


def test_nemo_root_is_directory():
    """.nemo/ must be a directory (not a file) after connect."""
    import nemo
    cwd = os.getcwd()
    name = "test_p1_nemodir"
    _cleanup(cwd, name)
    try:
        db = nemo.connect(name)
        nemo_root = _nemo_root(cwd)
        assert os.path.isdir(nemo_root), ".nemo is not a directory"
        db.close()
    finally:
        _cleanup(cwd, name)


def test_connect_no_path_param():
    """connect() must NOT accept a path= keyword argument."""
    import nemo
    import inspect
    sig = inspect.signature(nemo.connect)
    assert "path" not in sig.parameters, (
        "nemo.connect() must not have a 'path' parameter — "
        "all databases live in .nemo/<name>/ relative to CWD."
    )


def test_connect_no_timeout_param():
    """connect() must NOT accept a timeout= keyword argument."""
    import nemo
    import inspect
    sig = inspect.signature(nemo.connect)
    assert "timeout" not in sig.parameters, (
        "nemo.connect() must not have a 'timeout' parameter."
    )


def test_connect_accepts_buffer_pool_mb():
    """connect(name, buffer_pool_mb=N) must be accepted without error."""
    import nemo
    cwd = os.getcwd()
    name = "test_p1_bpm"
    _cleanup(cwd, name)
    try:
        db = nemo.connect(name, buffer_pool_mb=64)
        assert db is not None
        db.close()
    finally:
        _cleanup(cwd, name)


def test_reject_dot_extension():
    """Names with dots (e.g. 'mydb.nm') must raise ValueError."""
    import nemo
    with pytest.raises(ValueError, match="dots"):
        nemo.connect("mydb.nm")


def test_reject_path_separator_forward():
    """Names with '/' must raise ValueError."""
    import nemo
    with pytest.raises(ValueError, match="path separator"):
        nemo.connect("some/path")


def test_reject_path_separator_back():
    """Names with '\\' must raise ValueError."""
    import nemo
    with pytest.raises(ValueError, match="path separator"):
        nemo.connect("some\\path")


def test_reject_memory():
    """:memory: must raise ValueError — Nemo is disk-primary only."""
    import nemo
    with pytest.raises(ValueError, match="memory"):
        nemo.connect(":memory:")


def test_reject_empty_name():
    """Empty string must raise ValueError."""
    import nemo
    with pytest.raises(ValueError):
        nemo.connect("")


def test_reopen_existing_db():
    """Re-opening an already-created database must succeed without errors."""
    import nemo
    cwd = os.getcwd()
    name = "test_p1_reopen"
    _cleanup(cwd, name)
    try:
        db1 = nemo.connect(name)
        db1.close()
        # Second open — folder already exists, should succeed
        db2 = nemo.connect(name)
        assert os.path.isdir(_db_root(cwd, name))
        db2.close()
    finally:
        _cleanup(cwd, name)


def test_sql_roundtrip():
    """Basic CREATE TABLE + INSERT + SELECT round-trip must work after Phase 1."""
    import nemo
    cwd = os.getcwd()
    name = "test_p1_sql"
    _cleanup(cwd, name)
    try:
        db = nemo.connect(name)
        conn = db.connect_sync()
        conn.execute("CREATE TABLE p1_items (id INT, label TEXT);")
        conn.execute("INSERT INTO p1_items VALUES (1, 'alpha');")
        conn.execute("INSERT INTO p1_items VALUES (2, 'beta');")
        result = conn.execute("SELECT id, label FROM p1_items ORDER BY id;")
        rows = list(result)
        assert len(rows) == 2, f"Expected 2 rows, got {len(rows)}"
        assert rows[0][0] == 1 and rows[0][1] == "alpha"
        assert rows[1][0] == 2 and rows[1][1] == "beta"
        db.close()
    finally:
        _cleanup(cwd, name)


def test_database_name_exposed():
    """db.name must equal the plain name passed to connect()."""
    import nemo
    cwd = os.getcwd()
    name = "test_p1_nameattr"
    _cleanup(cwd, name)
    try:
        db = nemo.connect(name)
        assert db.name == name, f"db.name={db.name!r}, expected {name!r}"
        db.close()
    finally:
        _cleanup(cwd, name)


# ---------------------------------------------------------------------------
# Main entry point (also runnable with: python tests/test_phase1.py)
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    print("=" * 60)
    print("Phase 1: Storage Layout & Connect API Tests")
    print("=" * 60)
    sys.exit(pytest.main([__file__, "-v"]))
