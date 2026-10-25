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
import logging
import time
from typing import Optional, Callable, Any, Sequence

# Define standard loggers
logger = logging.getLogger("nemo")
db_logger = logging.getLogger("nemo.database")
conn_logger = logging.getLogger("nemo.connection")
txn_logger = logging.getLogger("nemo.transaction")
stmt_logger = logging.getLogger("nemo.statement")
pool_logger = logging.getLogger("nemo.pool")
async_logger = logging.getLogger("nemo.async")
result_logger = logging.getLogger("nemo.result")
native_logger = logging.getLogger("nemo.native")
perf_logger = logging.getLogger("nemo.performance")

def default_redaction(value: Any) -> str:
    if value is None:
        return "NULL"
    if isinstance(value, (int, float, bool)):
        return str(value)
    if isinstance(value, (list, tuple)):
        return f"ARRAY({len(value)} elements)"
    if isinstance(value, dict):
        return f"DICT({len(value)} elements)"
    if isinstance(value, bytes):
        return f"BLOB({len(value)} bytes)"
    if isinstance(value, str):
        return "<REDACTED_TEXT>"
    return f"<REDACTED_{type(value).__name__}>"

class LoggingConfig:
    def __init__(self, 
                 level: Optional[int] = None,
                 query_logging: Optional[bool] = None,
                 slow_query_threshold_ms: Optional[float] = None,
                 log_parameters: bool = False,
                 redaction: Optional[Callable[[Any], str]] = None,
                 native_logging: bool = False):
        
        # Read environment variables as fallbacks
        env_level_str = os.environ.get("NEMO_LOG_LEVEL")
        env_level = getattr(logging, env_level_str.upper()) if env_level_str and hasattr(logging, env_level_str.upper()) else None
        
        env_query_logging_str = os.environ.get("NEMO_QUERY_LOGGING")
        env_query_logging = env_query_logging_str.lower() in ("true", "1", "yes") if env_query_logging_str else False

        env_slow_query_str = os.environ.get("NEMO_SLOW_QUERY_MS")
        env_slow_query = float(env_slow_query_str) if env_slow_query_str else None

        self.level = level if level is not None else env_level
        self.query_logging = query_logging if query_logging is not None else env_query_logging
        self.slow_query_threshold_ms = slow_query_threshold_ms if slow_query_threshold_ms is not None else env_slow_query
        self.log_parameters = log_parameters
        self.redaction = redaction or default_redaction
        self.native_logging = native_logging

# Global configuration instance (defaults to silent/no special overrides)
_config = LoggingConfig()

def configure_logging(level: Optional[int] = None,
                      query_logging: Optional[bool] = None,
                      slow_query_threshold_ms: Optional[float] = None,
                      log_parameters: bool = False,
                      redaction: Optional[Callable[[Any], str]] = None,
                      native_logging: bool = False):
    global _config
    _config = LoggingConfig(
        level=level,
        query_logging=query_logging,
        slow_query_threshold_ms=slow_query_threshold_ms,
        log_parameters=log_parameters,
        redaction=redaction,
        native_logging=native_logging
    )
    
    if _config.level is not None:
        logger.setLevel(_config.level)
    else:
        logger.setLevel(logging.NOTSET)

def enable_debug():
    configure_logging(
        level=logging.DEBUG,
        query_logging=True
    )

def format_parameters(parameters: Sequence[object]) -> str:
    if not _config.log_parameters:
        return "<REDACTED_PARAMETERS>"
    if not parameters:
        return "()"
    return f"({', '.join(_config.redaction(p) for p in parameters)})"

def get_config() -> LoggingConfig:
    return _config

class QueryContext:
    def __init__(self, operation: str, sql: str, parameters: Sequence[object], conn_id: str, is_async: bool = False):
        self.operation = operation
        self.sql = sql
        self.parameters = parameters
        self.conn_id = conn_id
        self.is_async = is_async
        self.start_ns = 0

    def __enter__(self):
        self.start_ns = time.perf_counter_ns()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        duration_ns = time.perf_counter_ns() - self.start_ns
        duration_ms = duration_ns / 1_000_000.0

        if exc_type is not None:
            import asyncio
            if issubclass(exc_type, asyncio.CancelledError):
                if async_logger.isEnabledFor(logging.DEBUG):
                    async_logger.debug(
                        "Async query cancelled",
                        extra={
                            "operation": self.operation,
                            "conn_id": self.conn_id,
                            "duration_ms": duration_ms
                        }
                    )
            else:
                conn_logger.error(
                    "Query failed",
                    extra={
                        "operation": self.operation,
                        "conn_id": self.conn_id,
                        "duration_ms": duration_ms,
                        "error": str(exc_val)
                    }
                )
            return False

        if _config.slow_query_threshold_ms and duration_ms > _config.slow_query_threshold_ms:
            if perf_logger.isEnabledFor(logging.WARNING):
                perf_logger.warning(
                    f"Slow query detected: {duration_ms:.2f} ms",
                    extra={
                        "operation": self.operation,
                        "conn_id": self.conn_id,
                        "duration_ms": duration_ms
                    }
                )

        if _config.query_logging and conn_logger.isEnabledFor(logging.DEBUG):
            params_str = format_parameters(self.parameters)
            conn_logger.debug(
                f"SQL executed in {duration_ms:.2f} ms",
                extra={
                    "operation": self.operation,
                    "conn_id": self.conn_id,
                    "duration_ms": duration_ms,
                    "parameters": params_str
                }
            )
        return False
