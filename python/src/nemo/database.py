# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import asyncio
import logging
import pathlib
from typing import Optional
from .syncconnector import SyncConnection, SyncConnectionPool, SyncConnector, SyncConnectorPool
from .asyncconnector import AsyncConnection, AsyncConnectionPool, AsyncConnector, AsyncConnectorPool
from .logging import db_logger

try:
    from . import _nemo as _native
except ImportError:
    import _nemo as _native

# Characters not allowed in a database name
_FORBIDDEN_NAME_CHARS = set('/\\.:#@!*?<>|"\'')


def _validate_db_name(name: str) -> str:
    """
    Validate and return the cleaned database name.

    Rules:
      - Must be a non-empty string
      - No path separators (/ or \\)
      - No dots (no extensions allowed — just a plain name)
      - No '..' traversal
      - Not ':memory:'
    Raises ValueError with a descriptive message on any violation.
    """
    if not name or not isinstance(name, str):
        raise ValueError("Database name must be a non-empty string.")

    clean = name.strip()

    if clean == ":memory:":
        raise ValueError(
            "In-memory databases (:memory:) are not supported. "
            "Nemo is a strictly disk-primary embedded database."
        )

    if '/' in clean or '\\' in clean:
        raise ValueError(
            f"Invalid database name '{clean}': path separators (/ or \\) are not allowed. "
            "Provide only a plain name — Nemo manages the storage location."
        )

    if '.' in clean:
        raise ValueError(
            f"Invalid database name '{clean}': dots and extensions are not allowed. "
            "Use a plain name like 'mydb', not 'mydb.nm'."
        )

    if '..' in clean:
        raise ValueError(
            f"Invalid database name '{clean}': '..' is not allowed in a database name."
        )

    if not clean:
        raise ValueError("Invalid database name: name is empty after stripping.")

    return clean


def _resolve_db_path(name: str) -> dict:
    """
    Given a validated plain database name, resolve the full filesystem paths.

    Layout:
        <cwd>/.nemo/<name>/
            data.nm     — main data file
            wal/        — WAL segments directory
            lock        — process lock file

    Creates .nemo/ and .nemo/<name>/ and .nemo/<name>/wal/ if they do not exist.
    Returns a dict with keys: root, data, wal, lock
    """
    cwd = os.getcwd()
    nemo_root = os.path.join(cwd, ".nemo")
    db_root = os.path.join(nemo_root, name)
    wal_dir = os.path.join(db_root, "wal")
    data_file = os.path.join(db_root, "data.nm")
    lock_file = os.path.join(db_root, "lock")

    # Create .nemo/ if missing
    if not os.path.exists(nemo_root):
        os.makedirs(nemo_root, exist_ok=True)
    elif not os.path.isdir(nemo_root):
        raise OSError(
            f"'.nemo' exists at '{nemo_root}' but is not a directory. "
            "Remove or rename the file and try again."
        )

    # Create .nemo/<name>/ if missing
    if not os.path.exists(db_root):
        os.makedirs(db_root, exist_ok=True)

    # Create .nemo/<name>/wal/ if missing
    if not os.path.exists(wal_dir):
        os.makedirs(wal_dir, exist_ok=True)

    # Touch data.nm if it doesn't exist (native layer writes the real header)
    if not os.path.exists(data_file):
        import io
        with io.open(data_file, "ab"):
            pass

    return {
        "root": db_root,
        "data": data_file,
        "wal": wal_dir,
        "lock": lock_file,
    }


class Database:
    """
    Represents a Nemo database connection handle.

    Usage:
        db = nemo.connect("mydb")
        conn = db.connect()          # synchronous connection
        conn = db.connect_sync()     # same
        conn = db.connect_async()    # asyncio connection
    """

    def __init__(self, name: str, buffer_pool_mb: Optional[int] = None):
        """
        Open or create a Nemo database.

        Args:
            name:           Plain database name (no extension, no path).
                            Creates .nemo/<name>/ in the current working directory.
            buffer_pool_mb: Optional RAM limit for the buffer pool in megabytes.
                            Defaults to 1/4 of available system RAM (auto-detected).
        """
        clean_name = _validate_db_name(name)
        paths = _resolve_db_path(clean_name)

        self.name = clean_name
        self._paths = paths
        self._data_path = paths["data"]
        self.path = paths["root"]   # expose root folder as the database "path"
        self.is_memory = False
        self._buffer_pool_mb = buffer_pool_mb

        if db_logger.isEnabledFor(logging.INFO):
            db_logger.info(
                f"Connecting to Nemo database '{self.name}' at {paths['root']}",
                extra={"db_name": self.name, "data": self._data_path}
            )

        try:
            # Build a native DatabaseConfig so buffer_pool_mb flows into C++
            native_config = _native.DatabaseConfig()
            if buffer_pool_mb is not None and buffer_pool_mb > 0:
                # Pass as bytes; C++ side reads buffer_pool_size in bytes
                native_config.buffer_pool_size = int(buffer_pool_mb) * 1024 * 1024
            else:
                # 0 → C++ auto-detects 1/4 of available RAM
                native_config.buffer_pool_size = 0

            self._native = _native.open(self._data_path, native_config)
            # Expose native Database so tests can call db._db.statistics()
            self._db = self._native
            if db_logger.isEnabledFor(logging.INFO):
                db_logger.info(f"Nemo database '{self.name}' opened successfully")
        except Exception:
            db_logger.error(
                f"Failed to open Nemo database '{self.name}'", exc_info=True
            )
            raise

        # Phase 5: advisory process lock file — written after successful open.
        # Best-effort: failure does NOT prevent opening the database.
        self._lock_path = paths["lock"]
        self._lock_fd = None
        try:
            self._acquire_lock()
        except Exception:
            pass

    # --- Synchronous Connector / API ---
    def connect_sync(self) -> SyncConnection:
        return SyncConnection(self._native.connect())

    def connect(self) -> SyncConnection:
        return self.connect_sync()

    def sync_connector(self) -> SyncConnector:
        return self.connect_sync()

    def create_pool_sync(self, max_connections: int = 16) -> SyncConnectionPool:
        return SyncConnectionPool(self, max_connections)

    def sync_connector_pool(self, max_connections: int = 16) -> SyncConnectorPool:
        return self.create_pool_sync(max_connections)

    # --- Asynchronous Connector / API ---
    def connect_async(self) -> AsyncConnection:
        loop = asyncio.get_running_loop()
        return AsyncConnection(self._native.connect(), loop)

    def async_connector(self) -> AsyncConnector:
        return self.connect_async()

    def create_pool_async(self, max_connections: int = 16) -> AsyncConnectionPool:
        return AsyncConnectionPool(self, max_connections)

    def async_connector_pool(self, max_connections: int = 16) -> AsyncConnectorPool:
        return self.create_pool_async(max_connections)

    @staticmethod
    def engine_version() -> str:
        return _native.Database.engine_version()

    def flush(self):
        if hasattr(self._native, "flush"):
            self._native.flush()

    def checkpoint(self):
        if hasattr(self._native, "checkpoint"):
            self._native.checkpoint()

    # ------------------------------------------------------------------
    # Phase 5: Advisory process lock file
    # Uses an exclusive OS-level file lock on .nemo/<name>/lock so that
    # a second *process* trying to open the same DB sees the lock is held.
    # Threads within the same process share the lock (reference-counted).
    # Non-fatal: if locking fails the database still opens.
    # ------------------------------------------------------------------

    def _acquire_lock(self):
        """Write + advisory-lock the .nemo/<name>/lock file."""
        import sys
        try:
            self._lock_fd = open(self._lock_path, "w")
            self._lock_fd.write(str(os.getpid()))
            self._lock_fd.flush()
            # On POSIX use flock for advisory locking.
            # On Windows we rely on keeping the file open (advisory only;
            # msvcrt.locking blocks reads from other handles so we skip it).
            if sys.platform != "win32":
                import fcntl
                fcntl.flock(self._lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except Exception:
            # Best-effort: close fd if we opened it but couldn't lock
            if self._lock_fd is not None:
                try:
                    self._lock_fd.close()
                except Exception:
                    pass
                self._lock_fd = None

    def _release_lock(self):
        """Release and remove the advisory lock file."""
        import sys
        if self._lock_fd is None:
            return
        try:
            if sys.platform != "win32":
                import fcntl
                try:
                    fcntl.flock(self._lock_fd, fcntl.LOCK_UN)
                except Exception:
                    pass
            self._lock_fd.close()
            self._lock_fd = None
        except Exception:
            pass
        try:
            if os.path.exists(self._lock_path):
                os.remove(self._lock_path)
        except Exception:
            pass

    def close(self):
        if db_logger.isEnabledFor(logging.INFO):
            db_logger.info(f"Closing Nemo database '{self.name}'")
        self._native.close()
        self._release_lock()  # Phase 5: release advisory lock

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()


def connect(name: str, buffer_pool_mb: Optional[int] = None) -> Database:
    """
    Open or create a Nemo database by name.

    Creates `.nemo/<name>/` in the current working directory if it does not exist.

    Args:
        name:           Plain database name (letters, digits, underscores).
                        No extension, no path. Example: 'mydb', 'analytics', 'users'.
        buffer_pool_mb: Optional RAM limit for the buffer pool in megabytes.
                        Defaults to 1/4 of available system RAM.

    Returns:
        A Database instance ready for use.

    Raises:
        ValueError: If name contains a dot, path separator, or is otherwise invalid.

    Examples:
        db = nemo.connect("mydb")
        db = nemo.connect("mydb", buffer_pool_mb=512)
    """
    return Database(name=name, buffer_pool_mb=buffer_pool_mb)
