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

import logging
import uuid
from typing import Sequence, Optional, Iterator
from .logging import conn_logger, txn_logger, pool_logger, QueryContext

class SyncResult(Iterator):
    def __init__(self, native_result):
        self._native = native_result

    def fetchone(self) -> Optional[tuple]:
        return self._native.fetchone()

    def fetchmany(self, size: int) -> list[tuple]:
        return self._native.fetchmany(size)

    def fetchall(self) -> list[tuple]:
        return self._native.fetchall()

    def __iter__(self):
        return self

    def __next__(self) -> tuple:
        row = self.fetchone()
        if row is None:
            raise StopIteration
        return row

    @property
    def affected_rows(self) -> int:
        return self._native.affected_rows()

    @property
    def has_rows(self) -> bool:
        return self._native.has_rows()

    @property
    def columns(self):
        return self._native.columns()

    @property
    def statistics(self):
        return self._native.statistics()

class SyncPreparedStatement:
    def __init__(self, native_stmt, conn_id: str):
        self._native = native_stmt
        self._conn_id = conn_id

    def execute(self, *parameters: object) -> SyncResult:
        with QueryContext("EXECUTE_PREPARED", "<prepared_statement>", parameters, self._conn_id):
            return SyncResult(self._native.execute(parameters))

class SyncTransaction:
    def __init__(self, connection):
        self._conn = connection
        self._id = uuid.uuid4().hex[:8]
        self._native_txn = None
        self._active = False

    def begin(self):
        if not self._active:
            if txn_logger.isEnabledFor(logging.DEBUG):
                txn_logger.debug(f"Transaction {self._id} started", extra={"conn_id": self._conn._id, "txn_id": self._id})
            if hasattr(self._conn._native, "transaction"):
                self._native_txn = self._conn._native.transaction()
            self._active = True
        return self

    def commit(self):
        if self._active:
            if txn_logger.isEnabledFor(logging.DEBUG):
                txn_logger.debug(f"Transaction {self._id} commit", extra={"conn_id": self._conn._id, "txn_id": self._id})
            if self._native_txn is not None:
                self._native_txn.commit()
                self._native_txn = None
            self._active = False

    def rollback(self):
        if self._active:
            if txn_logger.isEnabledFor(logging.WARNING):
                txn_logger.warning(f"Transaction {self._id} rollback", extra={"conn_id": self._conn._id, "txn_id": self._id})
            if self._native_txn is not None:
                self._native_txn.rollback()
                self._native_txn = None
            self._active = False

    def __enter__(self):
        return self.begin()

    def savepoint(self, name: str):
        if self._native_txn is not None and hasattr(self._native_txn, "savepoint"):
            self._native_txn.savepoint(name)
        else:
            self._conn.execute(f"SAVEPOINT {name};")

    def rollback_to(self, name: str):
        if self._native_txn is not None and hasattr(self._native_txn, "rollback_to"):
            self._native_txn.rollback_to(name)
        else:
            self._conn.execute(f"ROLLBACK TO SAVEPOINT {name};")

    def release_savepoint(self, name: str):
        if self._native_txn is not None and hasattr(self._native_txn, "release_savepoint"):
            self._native_txn.release_savepoint(name)
        else:
            self._conn.execute(f"RELEASE SAVEPOINT {name};")

    def __exit__(self, exc_type, exc_val, exc_tb):
        if exc_type is None:
            self.commit()
        else:
            self.rollback()
        return False

class SyncConnection:
    def __init__(self, native_conn):
        self._native = native_conn
        self._id = uuid.uuid4().hex[:8]
        if conn_logger.isEnabledFor(logging.DEBUG):
            conn_logger.debug(f"Connection acquired", extra={"conn_id": self._id})

    def execute(self, sql: str, parameters: Optional[Sequence[object]] = None, timeout: float = 0.0) -> SyncResult:
        with QueryContext("EXECUTE", sql, parameters or (), self._id):
            return SyncResult(self._native.execute(sql, parameters or (), timeout))

    def executemany(self, sql: str, parameters_list: Sequence[Sequence[object]]):
        with QueryContext("EXECUTEMANY", sql, [], self._id):
            self._native.executemany(sql, parameters_list)

    def pop(self, table: str, count: int = 1, direction: str = "fifo") -> list[tuple]:
        return self._native.pop(table, count, direction)

    def count(self, table: str) -> int:
        return self._native.count(table)

    def truncate(self, table: str) -> int:
        return self._native.truncate(table)

    def explain(self, sql: str) -> str:
        if hasattr(self._native, "explain"):
            return self._native.explain(sql)
        res = self._native.execute(f"EXPLAIN {sql}")
        return "\n".join(r[0] for r in res.fetchall())

    def analyze(self, table: Optional[str] = None) -> bool:
        if hasattr(self._native, "analyze"):
            return self._native.analyze(table or "")
        q = f"ANALYZE {table}" if table else "ANALYZE"
        self._native.execute(q)
        return True

    def allocate_id_range(self, sequence_name: str, count: int = 1) -> tuple[int, int]:
        if hasattr(self._native, "allocate_id_range"):
            return self._native.allocate_id_range(sequence_name, count)
        res = self._native.execute(f"SELECT NEXTVAL_RANGE('{sequence_name}', {count})")
        row = res.fetchone()
        if row and isinstance(row[0], (list, tuple)):
            return tuple(row[0])
        elif row and isinstance(row[0], str):
            import json
            vals = json.loads(row[0])
            return (vals[0], vals[1])
        return (1, count)

    def extract_columnar(self, table: str, columns: Optional[Sequence[str]] = None, limit: int = 0) -> dict[str, list]:
        if hasattr(self._native, "extract_columnar"):
            return self._native.extract_columnar(table, columns, limit)
        cols_str = ", ".join(columns) if columns else "*"
        limit_str = f" LIMIT {limit}" if limit > 0 else ""
        res = self.execute(f"SELECT {cols_str} FROM {table}{limit_str}").fetchall()
        return {}

    def extract(self, table: str, columns: Optional[Sequence[str]] = None, where: Optional[str] = None, order_by: Optional[str] = None, limit: Optional[int] = None) -> list[tuple]:
        cols_str = ", ".join(columns) if columns else "*"
        sql = f"SELECT {cols_str} FROM {table}"
        if where:
            sql += f" WHERE {where}"
        if order_by:
            sql += f" ORDER BY {order_by}"
        if limit:
            sql += f" LIMIT {limit}"
        return self.execute(sql).fetchall()

    def prepare(self, sql: str) -> SyncPreparedStatement:
        if conn_logger.isEnabledFor(logging.DEBUG):
            conn_logger.debug(f"Prepared statement created", extra={"conn_id": self._id})
        return SyncPreparedStatement(self._native.prepare(sql), self._id)

    def transaction(self) -> SyncTransaction:
        return SyncTransaction(self)

    def begin_transaction(self) -> SyncTransaction:
        txn = SyncTransaction(self)
        return txn.begin()

    def commit(self):
        if hasattr(self._native, "commit"):
            self._native.commit()

    def rollback(self):
        if hasattr(self._native, "rollback"):
            self._native.rollback()

    def savepoint(self, name: str):
        return self.execute(f"SAVEPOINT {name};")

    def rollback_to(self, name: str):
        return self.execute(f"ROLLBACK TO SAVEPOINT {name};")

    def release_savepoint(self, name: str):
        return self.execute(f"RELEASE SAVEPOINT {name};")

    def set_autocommit(self, enabled: bool):
        if hasattr(self._native, "set_autocommit"):
            self._native.set_autocommit(enabled)

    def cancel(self):
        if hasattr(self._native, "cancel"):
            self._native.cancel()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def close(self):
        if conn_logger.isEnabledFor(logging.DEBUG):
            conn_logger.debug(f"Connection closed", extra={"conn_id": self._id})
        self._native.close()

class SyncConnectionPool:
    def __init__(self, db, max_connections: int = 16):
        self._db = db
        self._native = db._native.create_pool(max_connections)
        self._max_connections = max_connections
        if pool_logger.isEnabledFor(logging.INFO):
            pool_logger.info(f"Connection pool created", extra={"max_connections": max_connections})

    def acquire(self) -> SyncConnection:
        try:
            native_conn = self._native.acquire()
            return SyncConnection(native_conn)
        except Exception as e:
            pool_logger.warning("Connection pool exhaustion or acquisition failure", exc_info=True)
            raise

    connection = acquire

    def close(self):
        if pool_logger.isEnabledFor(logging.INFO):
            pool_logger.info(f"Connection pool closed")

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

# Aliases
SyncConnector = SyncConnection
SyncConnectorPool = SyncConnectionPool
