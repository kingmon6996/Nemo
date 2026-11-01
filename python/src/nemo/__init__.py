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

from .database import connect, Database
from .syncconnector import (
    SyncConnection, SyncConnectionPool, SyncPreparedStatement, SyncResult, SyncTransaction,
    SyncConnector, SyncConnectorPool
)
from .asyncconnector import (
    AsyncConnection, AsyncConnectionPool, AsyncPreparedStatement, AsyncResult, AsyncTransaction,
    AsyncConnector, AsyncConnectorPool
)
from .errors import (
    NemoError, DatabaseError, OperationalError, IntegrityError,
    ProgrammingError, TransactionError, ConcurrencyError, TimeoutError
)
from .logging import LoggingConfig, configure_logging, enable_debug

__version__ = "0.2.1"
__all__ = [
    "connect", "Database", 
    "SyncConnector", "SyncConnection", "SyncConnectionPool", "SyncConnectorPool",
    "SyncPreparedStatement", "SyncResult", "SyncTransaction",
    "AsyncConnector", "AsyncConnection", "AsyncConnectionPool", "AsyncConnectorPool",
    "AsyncPreparedStatement", "AsyncResult", "AsyncTransaction",
    "NemoError", "DatabaseError", "OperationalError",
    "IntegrityError", "ProgrammingError", "TransactionError", "ConcurrencyError",
    "TimeoutError",
    "LoggingConfig", "configure_logging", "enable_debug"
]
