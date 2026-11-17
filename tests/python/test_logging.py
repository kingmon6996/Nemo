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

import unittest
import logging
import asyncio
import os
import sys

try:
    import nemo
except ImportError:
    import unittest.mock as mock
    sys.modules['_nemo'] = mock.MagicMock()
    sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '../../python/src')))
    import nemo

# Mock for the native _nemo objects to test logging independently of C++ engine
class MockNativeResult:
    def fetchone(self): return None
    def fetchmany(self, size): return []
    def fetchall(self): return []

class MockNativeStatement:
    def execute(self, params): return MockNativeResult()

class MockNativeConnection:
    def execute(self, sql, params, timeout=0): return MockNativeResult()
    def executemany(self, sql, params_list): pass
    def prepare(self, sql): return MockNativeStatement()
    def close(self): pass
    def cancel(self): pass

class MockNativePool:
    def acquire(self): return MockNativeConnection()
    def close(self): pass

class MockNativeDatabase:
    def connect(self): return MockNativeConnection()
    def create_pool(self, max_conns): return MockNativePool()
    def close(self): pass

class RecordHandler(logging.Handler):
    def __init__(self):
        super().__init__()
        self.records = []
    
    def emit(self, record):
        self.records.append(record)

class TestLogging(unittest.TestCase):
    def setUp(self):
        # Capture log records
        self.handler = RecordHandler()
        logging.getLogger("nemo").addHandler(self.handler)
        # Reset logging config before each test
        nemo.configure_logging()

    def tearDown(self):
        logging.getLogger("nemo").removeHandler(self.handler)
        if os.path.exists("test_logging_db.nm"):
            try:
                os.remove("test_logging_db.nm")
            except OSError:
                pass

    def test_default_silence(self):
        # Ensure that without config, no logs are emitted for typical operations
        db = nemo.Database("test_logging_db")
        db._native = MockNativeDatabase() # Inject mock
        with db.connect_sync() as conn:
            conn.execute("SELECT 1")
        self.assertEqual(len(self.handler.records), 0)

    def test_debug_logging(self):
        nemo.configure_logging(level=logging.DEBUG, query_logging=True)
        db = nemo.Database("test_logging_db")
        db._native = MockNativeDatabase()
        with db.connect_sync() as conn:
            conn.execute("SELECT 1", (123,))
            
        messages = [r.getMessage() for r in self.handler.records]
        self.assertTrue(any("Connecting to Nemo database" in m for m in messages))
        self.assertIn("Connection acquired", messages)
        self.assertTrue(any("SQL executed in" in m for m in messages))
        self.assertIn("Connection closed", messages)
        
        # Check parameter redaction in LogRecord attributes
        query_record = next(r for r in self.handler.records if "SQL executed in" in r.getMessage())
        self.assertEqual(query_record.parameters, "<REDACTED_PARAMETERS>")

    def test_parameter_redaction(self):
        nemo.configure_logging(level=logging.DEBUG, query_logging=True, log_parameters=True)
        db = nemo.Database("test_logging_db")
        db._native = MockNativeDatabase()
        with db.connect_sync() as conn:
            conn.execute("INSERT INTO users VALUES (?, ?, ?)", ("my_password", b"raw_data", {"key": "val"}))
            
        query_record = next(r for r in self.handler.records if "SQL executed in" in r.getMessage())
        params_str = query_record.parameters
        
        # Ensure password string is redacted
        self.assertIn("<REDACTED_TEXT>", params_str)
        # Ensure BLOB is redacted
        self.assertIn("BLOB(", params_str)
        # Ensure DICT is redacted to size
        self.assertIn("DICT(", params_str)
        self.assertNotIn("my_password", params_str)
        self.assertNotIn("raw_data", params_str)
        self.assertNotIn("key", params_str)

    def test_async_logging(self):
        nemo.configure_logging(level=logging.DEBUG, query_logging=True)
        db = nemo.Database("test_logging_db")
        db._native = MockNativeDatabase()
        
        async def run_async():
            async with db.connect_async() as conn:
                await conn.execute("SELECT 1")
                
        asyncio.run(run_async())
        messages = [r.getMessage() for r in self.handler.records]
        self.assertIn("Async Connection acquired", messages)
        self.assertTrue(any("SQL executed in" in m for m in messages))
        self.assertIn("Async Connection closed", messages)

    def test_pool_logging(self):
        nemo.configure_logging(level=logging.INFO)
        db = nemo.Database("test_logging_db")
        db._native = MockNativeDatabase()
        
        with db.create_pool_sync(max_connections=5) as pool:
            conn = pool.acquire()
            conn.close()
            
        messages = [r.getMessage() for r in self.handler.records]
        self.assertIn("Connection pool created", messages)
        self.assertIn("Connection pool closed", messages)

    def test_slow_query_logging(self):
        # We need a custom mock for this that takes time
        class SlowMockNativeConnection(MockNativeConnection):
            def execute(self, sql, params, timeout=0):
                import time
                time.sleep(0.015) # 15 ms
                return MockNativeResult()
                
        nemo.configure_logging(level=logging.WARNING, slow_query_threshold_ms=10.0)
        db = nemo.Database("test_logging_db")
        db._native = MockNativeDatabase()
        db._native.connect = lambda: SlowMockNativeConnection()
        
        with db.connect_sync() as conn:
            conn.execute("SELECT SLOW")
            
        messages = [r.getMessage() for r in self.handler.records]
        self.assertTrue(any("Slow query detected:" in m for m in messages))

if __name__ == "__main__":
    unittest.main()
