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

import asyncio
import logging
import uuid
from typing import Sequence, Optional, AsyncIterator
from .logging import conn_logger, txn_logger, pool_logger, async_logger, QueryContext

class AsyncResult(AsyncIterator):
    def __init__(self, native_result, loop):
        self._native = native_result
        self._loop = loop

    async def fetchone(self) -> Optional[tuple]:
        return await self._loop.run_in_executor(None, self._native.fetchone)

    async def fetchmany(self, size: int) -> list[tuple]:
        return await self._loop.run_in_executor(None, self._native.fetchmany, size)

    async def fetchall(self) -> list[tuple]:
        return await self._loop.run_in_executor(None, self._native.fetchall)

    def __aiter__(self):
        return self

    async def __anext__(self) -> tuple:
        row = await self.fetchone()
        if row is None:
            raise StopAsyncIteration
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

class AsyncPreparedStatement:
    def __init__(self, native_stmt, loop, conn_id: str):
        self._native = native_stmt
        self._loop = loop
        self._conn_id = conn_id

    async def execute(self, *parameters: object) -> AsyncResult:
        with QueryContext("EXECUTE_PREPARED_ASYNC", "<prepared_statement>", parameters, self._conn_id, is_async=True):
            native_result = await self._loop.run_in_executor(None, self._native.execute, parameters)
            return AsyncResult(native_result, self._loop)

class AsyncTransaction:
    def __init__(self, connection):
        self._conn = connection
        self._id = uuid.uuid4().hex[:8]
        self._native_txn = None
        self._active = False

    async def begin(self):
        if not self._active:
            if txn_logger.isEnabledFor(logging.DEBUG):
                txn_logger.debug(f"Async Transaction {self._id} started", extra={"conn_id": self._conn._id, "txn_id": self._id})
            if hasattr(self._conn._native, "transaction"):
                self._native_txn = await self._conn._loop.run_in_executor(None, self._conn._native.transaction)
            self._active = True
        return self

    async def commit(self):
        if self._active:
            if txn_logger.isEnabledFor(logging.DEBUG):
                txn_logger.debug(f"Async Transaction {self._id} commit", extra={"conn_id": self._conn._id, "txn_id": self._id})
            if self._native_txn is not None:
                await self._conn._loop.run_in_executor(None, self._native_txn.commit)
                self._native_txn = None
            self._active = False

    async def rollback(self):
        if self._active:
            if txn_logger.isEnabledFor(logging.WARNING):
                txn_logger.warning(f"Async Transaction {self._id} rollback", extra={"conn_id": self._conn._id, "txn_id": self._id})
            if self._native_txn is not None:
                await self._conn._loop.run_in_executor(None, self._native_txn.rollback)
                self._native_txn = None
            self._active = False

    async def __aenter__(self):
        return await self.begin()

    async def savepoint(self, name: str):
        if self._native_txn is not None and hasattr(self._native_txn, "savepoint"):
            await self._conn._loop.run_in_executor(None, self._native_txn.savepoint, name)
        else:
            await self._conn.execute(f"SAVEPOINT {name};")

    async def rollback_to(self, name: str):
        if self._native_txn is not None and hasattr(self._native_txn, "rollback_to"):
            await self._conn._loop.run_in_executor(None, self._native_txn.rollback_to, name)
        else:
            await self._conn.execute(f"ROLLBACK TO SAVEPOINT {name};")

    async def release_savepoint(self, name: str):
        if self._native_txn is not None and hasattr(self._native_txn, "release_savepoint"):
            await self._conn._loop.run_in_executor(None, self._native_txn.release_savepoint, name)
        else:
            await self._conn.execute(f"RELEASE SAVEPOINT {name};")

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        if exc_type is None:
            await self.commit()
        else:
            await self.rollback()
        return False

class AsyncConnection:
    def __init__(self, native_conn, loop):
        self._native = native_conn
        self._loop = loop
        self._id = uuid.uuid4().hex[:8]
        if conn_logger.isEnabledFor(logging.DEBUG):
            conn_logger.debug(f"Async Connection acquired", extra={"conn_id": self._id})

    async def execute(self, sql: str, parameters: Optional[Sequence[object]] = None, timeout: float = 0.0) -> AsyncResult:
        with QueryContext("EXECUTE_ASYNC", sql, parameters or (), self._id, is_async=True):
            task = self._loop.run_in_executor(None, self._native.execute, sql, parameters or (), timeout)
            try:
                native_result = await task
                return AsyncResult(native_result, self._loop)
            except asyncio.CancelledError:
                if async_logger.isEnabledFor(logging.DEBUG):
                    async_logger.debug("Async task cancelled, cancelling native operation")
                self._native.cancel()
                raise

    async def executemany(self, sql: str, parameters_list: Sequence[Sequence[object]]):
        with QueryContext("EXECUTEMANY_ASYNC", sql, [], self._id, is_async=True):
            task = self._loop.run_in_executor(None, self._native.executemany, sql, parameters_list)
            try:
                await task
            except asyncio.CancelledError:
                if async_logger.isEnabledFor(logging.DEBUG):
                    async_logger.debug("Async executemany task cancelled, cancelling native operation")
                self._native.cancel()
                raise

    async def pop(self, table: str, count: int = 1, direction: str = "fifo") -> list[tuple]:
        return await self._loop.run_in_executor(None, self._native.pop, table, count, direction)

    async def count(self, table: str) -> int:
        return await self._loop.run_in_executor(None, self._native.count, table)

    async def truncate(self, table: str) -> int:
        return await self._loop.run_in_executor(None, self._native.truncate, table)

    async def explain(self, sql: str) -> str:
        if hasattr(self._native, "explain"):
            return await self._loop.run_in_executor(None, self._native.explain, sql)
        res = await self.execute(f"EXPLAIN {sql}")
        rows = await self._loop.run_in_executor(None, res.fetchall)
        return "\n".join(r[0] for r in rows)

    async def analyze(self, table: Optional[str] = None) -> bool:
        if hasattr(self._native, "analyze"):
            return await self._loop.run_in_executor(None, self._native.analyze, table or "")
        q = f"ANALYZE {table}" if table else "ANALYZE"
        await self.execute(q)
        return True

    async def allocate_id_range(self, sequence_name: str, count: int = 1) -> tuple[int, int]:
        if hasattr(self._native, "allocate_id_range"):
            return await self._loop.run_in_executor(None, self._native.allocate_id_range, sequence_name, count)
        res = await self.execute(f"SELECT NEXTVAL_RANGE('{sequence_name}', {count})")
        row = await res.fetchone()
        if row and isinstance(row[0], (list, tuple)):
            return tuple(row[0])
        elif row and isinstance(row[0], str):
            import json
            vals = json.loads(row[0])
            return (vals[0], vals[1])
        return (1, count)

    async def extract_columnar(self, table: str, columns: Optional[Sequence[str]] = None, limit: int = 0) -> dict[str, list]:
        if hasattr(self._native, "extract_columnar"):
            return await self._loop.run_in_executor(None, self._native.extract_columnar, table, columns, limit)
        return {}

    async def extract(self, table: str, columns: Optional[Sequence[str]] = None, where: Optional[str] = None, order_by: Optional[str] = None, limit: Optional[int] = None) -> list[tuple]:
        cols_str = ", ".join(columns) if columns else "*"
        sql = f"SELECT {cols_str} FROM {table}"
        if where:
            sql += f" WHERE {where}"
        if order_by:
            sql += f" ORDER BY {order_by}"
        if limit:
            sql += f" LIMIT {limit}"
        res = await self.execute(sql)
        return await self._loop.run_in_executor(None, res.fetchall)

    async def prepare(self, sql: str) -> AsyncPreparedStatement:
        if conn_logger.isEnabledFor(logging.DEBUG):
            conn_logger.debug(f"Async Prepared statement created", extra={"conn_id": self._id})
        native_stmt = await self._loop.run_in_executor(None, self._native.prepare, sql)
        return AsyncPreparedStatement(native_stmt, self._loop, self._id)

    def transaction(self) -> AsyncTransaction:
        return AsyncTransaction(self)

    async def begin_transaction(self) -> AsyncTransaction:
        txn = AsyncTransaction(self)
        return await txn.begin()

    async def commit(self):
        if hasattr(self._native, "commit"):
            await self._loop.run_in_executor(None, self._native.commit)

    async def rollback(self):
        if hasattr(self._native, "rollback"):
            await self._loop.run_in_executor(None, self._native.rollback)

    async def savepoint(self, name: str):
        return await self.execute(f"SAVEPOINT {name};")

    async def rollback_to(self, name: str):
        return await self.execute(f"ROLLBACK TO SAVEPOINT {name};")

    async def release_savepoint(self, name: str):
        return await self.execute(f"RELEASE SAVEPOINT {name};")

    async def set_autocommit(self, enabled: bool):
        if hasattr(self._native, "set_autocommit"):
            await self._loop.run_in_executor(None, self._native.set_autocommit, enabled)

    def cancel(self):
        if hasattr(self._native, "cancel"):
            self._native.cancel()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        await self.close()

    async def close(self):
        if conn_logger.isEnabledFor(logging.DEBUG):
            conn_logger.debug(f"Async Connection closed", extra={"conn_id": self._id})
        await self._loop.run_in_executor(None, self._native.close)

class _AsyncPoolAcquireContextManager:
    def __init__(self, pool):
        self._pool = pool
        self._conn = None

    def __await__(self):
        return self._acquire().__await__()

    async def _acquire(self) -> AsyncConnection:
        try:
            native_conn = await self._pool._loop.run_in_executor(None, self._pool._native.acquire)
            return AsyncConnection(native_conn, self._pool._loop)
        except Exception:
            pool_logger.warning("Async Connection pool exhaustion or acquisition failure", exc_info=True)
            raise

    async def __aenter__(self) -> AsyncConnection:
        self._conn = await self._acquire()
        return self._conn

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        if self._conn is not None:
            await self._conn.close()

class AsyncConnectionPool:
    def __init__(self, db, max_connections: int = 16):
        self._db = db
        self._loop = asyncio.get_running_loop()
        self._native = db._native.create_pool(max_connections)
        self._max_connections = max_connections
        if pool_logger.isEnabledFor(logging.INFO):
            pool_logger.info(f"Async Connection pool created", extra={"max_connections": max_connections})

    def acquire(self):
        return _AsyncPoolAcquireContextManager(self)

    connection = acquire

    def close(self):
        if pool_logger.isEnabledFor(logging.INFO):
            pool_logger.info("Async Connection pool closed")

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        await self.close()

# Aliases
AsyncConnector = AsyncConnection
AsyncConnectorPool = AsyncConnectionPool
