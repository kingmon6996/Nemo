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
import os
import shutil
import asyncio
import sys

# Ensure package directory is in sys.path
sys.path.insert(0, os.path.abspath("package"))
import nemo

class TestAllFeatures(unittest.TestCase):
    db_name = "test_features_db"

    @classmethod
    def setUpClass(cls):
        shutil.rmtree(os.path.join(".nemo", cls.db_name), ignore_errors=True)
        for ext in [".nm", ".nm.meta"]:
            p = cls.db_name + ext
            if os.path.exists(p):
                try: os.remove(p)
                except OSError: pass

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(os.path.join(".nemo", cls.db_name), ignore_errors=True)
        for ext in [".nm", ".nm.meta"]:
            p = cls.db_name + ext
            if os.path.exists(p):
                try: os.remove(p)
                except OSError: pass

    def setUp(self):
        self.db_name = f"test_{self._testMethodName}_db"
        shutil.rmtree(os.path.join(".nemo", self.db_name), ignore_errors=True)
        for ext in [".nm", ".nm.meta"]:
            p = self.db_name + ext
            if os.path.exists(p):
                try: os.remove(p)
                except OSError: pass
        self.db = nemo.Database(self.db_name)
        self.conn = self.db.connect_sync()

    def tearDown(self):
        try:
            self.conn.close()
        except Exception:
            pass
        try:
            self.db.close()
        except Exception:
            pass
        shutil.rmtree(os.path.join(".nemo", self.db_name), ignore_errors=True)
        for ext in [".nm", ".nm.meta"]:
            p = self.db_name + ext
            if os.path.exists(p):
                try: os.remove(p)
                except OSError: pass

    # =========================================================================
    # 1. New Datatypes
    # =========================================================================
    def test_datatypes_creation_and_insertion(self):
        """Test CREATE TABLE with JSONB, ENUM, CHAR, BIT, VARBIT, INET, CIDR, RANGE."""
        sql_create = """
        CREATE TABLE complex_types (
            id BIGINT AUTOINCREMENT PRIMARY KEY,
            doc JSONB,
            status ENUM,
            code CHAR(5),
            mask BIT(8),
            vmask VARBIT(16),
            client_ip INET,
            subnet CIDR,
            active_range RANGE
        )
        """
        self.conn.execute(sql_create)

        # Insert record with all new types
        sql_insert = """
        INSERT INTO complex_types (doc, status, code, mask, vmask, client_ip, subnet, active_range)
        VALUES ('{"tag": "alpha", "level": 1}', 'active', 'ABCDE', '10101010', '110011', '192.168.1.1', '10.0.0.0/24', '[1, 100)')
        """
        res = self.conn.execute(sql_insert)
        self.assertEqual(res.affected_rows, 1)

        # Query back and verify
        rows = self.conn.execute("SELECT id, doc, status, code, mask, client_ip FROM complex_types").fetchall()
        self.assertEqual(len(rows), 1)
        row = rows[0]
        self.assertEqual(row[0], 1)
        self.assertIn("alpha", str(row[1]))
        self.assertEqual(str(row[2]), "active")
        self.assertEqual(str(row[3]), "ABCDE")
        self.assertEqual(str(row[4]), "10101010")
        self.assertEqual(str(row[5]), "192.168.1.1")

    # =========================================================================
    # 2. JSON & JSONB Extraction Functions
    # =========================================================================
    def test_json_functions(self):
        # json_extract, json_value, json_keys, json_length, json_contains
        res = self.conn.execute("SELECT json_extract('{\"name\": \"Alice\", \"age\": 30}', '$.name')")
        self.assertEqual(res.fetchone()[0], "Alice")

        res = self.conn.execute("SELECT json_value('{\"user\": {\"city\": \"London\"}}', '$.user.city')")
        self.assertEqual(res.fetchone()[0], "London")

        res = self.conn.execute("SELECT json_keys('{\"a\": 1, \"b\": 2}')")
        keys = res.fetchone()[0]
        self.assertIn("a", str(keys))
        self.assertIn("b", str(keys))

        res = self.conn.execute("SELECT json_length('[10, 20, 30, 40]')")
        self.assertEqual(res.fetchone()[0], 4)

        res = self.conn.execute("SELECT json_contains('{\"role\": \"admin\"}', 'admin')")
        self.assertEqual(res.fetchone()[0], True)

    # =========================================================================
    # 3. ARRAY Functions
    # =========================================================================
    def test_array_functions(self):
        # array_length, array_get, array_append, array_slice, array_contains, array_sum
        res = self.conn.execute("SELECT array_length('[1, 2, 3, 4, 5]')")
        self.assertEqual(res.fetchone()[0], 5)

        res = self.conn.execute("SELECT array_get('[10, 20, 30]', 2)")
        self.assertEqual(res.fetchone()[0], 20)

        res = self.conn.execute("SELECT array_append('[1, 2]', 3)")
        self.assertIn("3", str(res.fetchone()[0]))

        res = self.conn.execute("SELECT array_contains('[10, 20, 30]', 20)")
        self.assertEqual(res.fetchone()[0], True)

        res = self.conn.execute("SELECT array_sum('[10, 20, 30]')")
        self.assertEqual(res.fetchone()[0], 60)

    # =========================================================================
    # 4. DICT Functions
    # =========================================================================
    def test_dict_functions(self):
        # dict_get, dict_has_key, dict_size, dict_merge
        res = self.conn.execute("SELECT dict_get('{\"x\": 100, \"y\": 200}', 'x')")
        self.assertEqual(res.fetchone()[0], 100)

        res = self.conn.execute("SELECT dict_has_key('{\"x\": 100}', 'x')")
        self.assertEqual(res.fetchone()[0], True)

        res = self.conn.execute("SELECT dict_size('{\"a\": 1, \"b\": 2, \"c\": 3}')")
        self.assertEqual(res.fetchone()[0], 3)

    # =========================================================================
    # 5. String, Math, Binary & Utility Functions
    # =========================================================================
    def test_scalar_utility_functions(self):
        # UPPER, LOWER, CONCAT, COALESCE, ROUND, ABS, HEX
        res = self.conn.execute("SELECT upper('nemo'), lower('ENGINE'), concat('Fast', 'Relational')")
        row = res.fetchone()
        self.assertEqual(row[0], "NEMO")
        self.assertEqual(row[1], "engine")
        self.assertEqual(row[2], "FastRelational")

        res = self.conn.execute("SELECT coalesce(NULL, 'fallback'), round(3.14159, 2), abs(-42)")
        row = res.fetchone()
        self.assertEqual(row[0], "fallback")
        self.assertEqual(row[1], 3.14)
        self.assertEqual(row[2], 42)

        res = self.conn.execute("SELECT hex_encode('nemo')")
        self.assertEqual(res.fetchone()[0], "6e656d6f")

    # =========================================================================
    # 6. Auto-Incrementing Primary Keys & Multi-row Inserts
    # =========================================================================
    def test_autoincrement_and_conflict_reconciliation(self):
        self.conn.execute("CREATE TABLE users (id BIGINT AUTOINCREMENT PRIMARY KEY, name TEXT, role TEXT)")

        # Multi-row insert omitting the ID column -> IDs 1, 2, 3
        sql_multi = "INSERT INTO users (name, role) VALUES ('Alice', 'Dev'), ('Bob', 'QA'), ('Charlie', 'Ops')"
        res = self.conn.execute(sql_multi)
        self.assertEqual(res.affected_rows, 3)

        rows = self.conn.execute("SELECT id, name FROM users").fetchall()
        self.assertEqual(len(rows), 3)
        self.assertEqual(rows[0][0], 1)
        self.assertEqual(rows[1][0], 2)
        self.assertEqual(rows[2][0], 3)

        # Explicit ID insert -> ID 10
        self.conn.execute("INSERT INTO users (id, name, role) VALUES (10, 'ExplicitAdmin', 'Admin')")

        # Next insert omitting ID should automatically reconcile and jump to 11 (no conflict!)
        self.conn.execute("INSERT INTO users (name, role) VALUES ('David', 'Dev')")

        rows = self.conn.execute("SELECT id, name FROM users").fetchall()
        self.assertEqual(len(rows), 5)
        self.assertEqual(rows[3][0], 10)
        self.assertEqual(rows[4][0], 11)

    # =========================================================================
    # 7. Database-Managed Sequences & Range Allocation
    # =========================================================================
    def test_sequences_and_range_allocation(self):
        # CREATE SEQUENCE
        self.conn.execute("CREATE SEQUENCE order_seq START WITH 100 INCREMENT BY 5")

        # SELECT NEXTVAL('order_seq') -> 100
        res = self.conn.execute("SELECT nextval('order_seq')")
        self.assertEqual(res.fetchone()[0], 100)

        # NEXTVAL -> 105
        res = self.conn.execute("SELECT nextval('order_seq')")
        self.assertEqual(res.fetchone()[0], 105)

        # CURRVAL -> 105
        res = self.conn.execute("SELECT currval('order_seq')")
        self.assertEqual(res.fetchone()[0], 105)

        # allocate_id_range API on Connection:
        # Allocate 10 IDs from a new sequence
        start_id, end_id = self.conn.allocate_id_range("ticket_seq", 10)
        self.assertEqual(start_id, 1)
        self.assertEqual(end_id, 10)

        # Next allocation
        start_id2, end_id2 = self.conn.allocate_id_range("ticket_seq", 5)
        self.assertEqual(start_id2, 11)
        self.assertEqual(end_id2, 15)

    # =========================================================================
    # 8. Persistence across Database Close and Reopen
    # =========================================================================
    def test_state_persistence_across_restart(self):
        # Create table and insert rows
        self.conn.execute("CREATE TABLE durable_tbl (id BIGINT AUTOINCREMENT PRIMARY KEY, title TEXT)")
        self.conn.execute("INSERT INTO durable_tbl (title) VALUES ('Item A'), ('Item B')")
        self.conn.allocate_id_range("durable_seq", 50) # allocated 1..50

        # Close connection and database
        self.conn.close()
        self.db.close()

        # Reopen database
        db_reopened = nemo.Database(self.db_name)
        conn_reopened = db_reopened.connect_sync()

        # Query durable table
        rows = conn_reopened.execute("SELECT id, title FROM durable_tbl").fetchall()
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0][0], 1)
        self.assertEqual(rows[1][0], 2)

        # Insert new row: ID must continue from 3
        conn_reopened.execute("INSERT INTO durable_tbl (title) VALUES ('Item C')")
        row_c = conn_reopened.execute("SELECT id, title FROM durable_tbl WHERE title = 'Item C'").fetchall()
        self.assertEqual(row_c[0][0], 3)

        # Sequence must continue from 51
        start, end = conn_reopened.allocate_id_range("durable_seq", 10)
        self.assertEqual(start, 51)
        self.assertEqual(end, 60)

        conn_reopened.close()
        db_reopened.close()

    # =========================================================================
    # 9. Asynchronous Connector Support
    # =========================================================================
    def test_async_connector(self):
        self.conn.close()
        async def run_async():
            async with self.db.connect_async() as aconn:
                await aconn.execute("CREATE TABLE async_items (id BIGINT AUTOINCREMENT PRIMARY KEY, name TEXT)")
                await aconn.execute("INSERT INTO async_items (name) VALUES ('Async1'), ('Async2')")
                res = await aconn.execute("SELECT id, name FROM async_items")
                rows = await res.fetchall()
                self.assertEqual(len(rows), 2)

                # Async allocate_id_range
                s, e = await aconn.allocate_id_range("async_seq", 20)
                self.assertEqual(s, 1)
                self.assertEqual(e, 20)

        asyncio.run(run_async())
        self.conn = self.db.connect_sync()


if __name__ == "__main__":
    unittest.main()
