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
from typing import Optional
from .syncconnector import SyncConnection, SyncConnectionPool, SyncConnector, SyncConnectorPool
from .asyncconnector import AsyncConnection, AsyncConnectionPool, AsyncConnector, AsyncConnectorPool
from .logging import db_logger

try:
    from . import _nemo as _native
except ImportError:
    import _nemo as _native

class Database:
    def __init__(self, name: str, path: Optional[str] = None, timeout: float = 0.0):
        if not name or not isinstance(name, str):
            raise ValueError("Database name must be a non-empty string.")

        clean_name = name.strip()
        if clean_name == ":memory:":
            raise ValueError("In-memory databases (:memory:) are not supported. Nemo is strictly an embedded local persistent database.")

        if path is not None and (os.path.isdir(clean_name) or clean_name.endswith(("/", "\\"))):
            clean_name, path = path.strip(), clean_name

        # Strip any extension if accidentally provided (only name is accepted)
        if clean_name.endswith(".nm"):
            clean_name = clean_name[:-3]
        clean_name = os.path.splitext(clean_name)[0]

        if not clean_name:
            raise ValueError("Invalid database name.")

        self.name = clean_name
        self.timeout = float(timeout)

        if path is not None:
            full_path = os.path.join(path, f"{self.name}.nm")
        else:
            full_path = f"{self.name}.nm"

        self._path = os.path.abspath(full_path)
        self.path = self._path
        self.is_memory = False

        # Ensure parent directory and touch file for local persistent storage
        parent_dir = os.path.dirname(self._path)
        if parent_dir and not os.path.exists(parent_dir):
            os.makedirs(parent_dir, exist_ok=True)

        if not os.path.exists(self._path):
            import io
            with io.open(self._path, "ab"):
                pass

        if db_logger.isEnabledFor(logging.INFO):
            db_logger.info(f"Connecting to Nemo database: {self._path}", extra={"path_len": len(self._path), "timeout": self.timeout})
        
        try:
            self._native = _native.open(self._path)
            if db_logger.isEnabledFor(logging.INFO):
                db_logger.info("Nemo database connected successfully")
        except Exception as e:
            db_logger.error("Failed to connect to Nemo database", exc_info=True)
            raise

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

    def connect_async(self) -> AsyncConnection:
        loop = asyncio.get_running_loop()
        return AsyncConnection(self._native.connect(), loop)

    def async_connector(self) -> AsyncConnector:
        return self.connect_async()

    def create_pool_async(self, max_connections: int = 16) -> AsyncConnectionPool:
        return AsyncConnectionPool(self, max_connections)

    def async_connector_pool(self, max_connections: int = 16) -> AsyncConnectorPool:
        return self.create_pool_async(max_connections)

    def close(self):
        if db_logger.isEnabledFor(logging.INFO):
            db_logger.info("Closing Nemo database")
        self._native.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

def connect(name: str, path: Optional[str] = None, timeout: float = 0.0) -> Database:
    return Database(name=name, path=path, timeout=timeout)
