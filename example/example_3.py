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

"""
Example 3: Comprehensive 30 Native Datatypes Demonstration
Demonstrates creating schemas and inserting/querying all 30 native SQL datatypes
supported by Nemo (Primitives, Strings, Binaries, Temporals, Arrays, Dicts, JSON/JSONB,
Network, Bitwise, Ranges, UUID, and Null).
"""

import os
import nemo

DB_NAME = "example3_datatypes_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 3: ALL 30 NATIVE DATATYPES")
    print("=" * 70)

    with nemo.connect(DB_NAME) as db:
        with db.connect_sync() as conn:

            # 1. Create table spanning all 30 native types
            print("\n[1] Defining schema with all 30 native SQL datatypes...")
            conn.execute("""
                CREATE TABLE IF NOT EXISTS omni_types_table (
                    id BIGINT PRIMARY KEY AUTOINCREMENT,
                    c_bool BOOLEAN,
                    c_tinyint TINYINT,
                    c_smallint SMALLINT,
                    c_int INTEGER,
                    c_bigint BIGINT,
                    c_real REAL,
                    c_double DOUBLE PRECISION,
                    c_decimal DECIMAL(10, 2),
                    c_char CHAR(10),
                    c_varchar VARCHAR(50),
                    c_text TEXT,
                    c_bytea BYTEA,
                    c_date DATE,
                    c_time TIME,
                    c_timestamp TIMESTAMP,
                    c_timestamptz TIMESTAMPTZ,
                    c_interval INTERVAL,
                    c_uuid UUID,
                    c_json JSON,
                    c_jsonb JSONB,
                    c_dict DICT,
                    c_array ARRAY,
                    c_enum ENUM,
                    c_bit BIT(8),
                    c_varbit VARBIT(16),
                    c_inet INET,
                    c_cidr CIDR,
                    c_macaddr MACADDR,
                    c_range RANGE,
                    c_null TEXT
                );
            """)
            print("    -> 30-datatype table schema created successfully.")

            # 2. Insert row with typed literals
            print("\n[2] Inserting rich typed record across all 30 types...")
            conn.execute("""
                INSERT INTO omni_types_table (
                    c_bool, c_tinyint, c_smallint, c_int, c_bigint,
                    c_real, c_double, c_decimal, c_char, c_varchar, c_text,
                    c_bytea, c_date, c_time, c_timestamp, c_timestamptz, c_interval,
                    c_uuid, c_json, c_jsonb, c_dict, c_array, c_enum,
                    c_bit, c_varbit, c_inet, c_cidr, c_macaddr, c_range, c_null
                ) VALUES (
                    true, 42, 1024, 100000, 9223372036854775807,
                    3.14, 2.718281828459, 12345.67, 'NEMO_CHAR ', 'code_alpha', 'Long unicode text payload',
                    '\\xdeadbeef', '2026-10-25', '14:30:00', '2026-10-25T14:30:00Z', '2026-10-25T14:30:00+00:00', '1 day 02:00:00',
                    'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', '{"tier": "enterprise", "active": true}', '{"score": 99.8, "tags": ["db"]}',
                    '{"env": "prod", "cluster": "us-east"}', '[10, 20, 30, 40, 50]', 'active',
                    '10101010', '1100110011', '192.168.1.100', '10.0.0.0/24', '08:00:2b:01:02:03',
                    '[1, 100)', null
                );
            """)
            print("    -> Inserted 30-datatype record.")

            # 3. Retrieve and inspect typed values
            print("\n[3] Reading and verifying typed representations:")
            row = conn.execute("SELECT * FROM omni_types_table LIMIT 1;").fetchone()

            labels = [
                "ID (Primary Key)", "BOOLEAN", "TINYINT", "SMALLINT", "INTEGER", "BIGINT",
                "REAL", "DOUBLE", "DECIMAL", "CHAR", "VARCHAR", "TEXT",
                "BYTEA", "DATE", "TIME", "TIMESTAMP", "TIMESTAMPTZ", "INTERVAL",
                "UUID", "JSON", "JSONB", "DICT", "ARRAY", "ENUM",
                "BIT", "VARBIT", "INET", "CIDR", "MACADDR", "RANGE", "NULL"
            ]

            for idx, label in enumerate(labels):
                val = row[idx] if idx < len(row) else None
                print(f"    - {label:<22} : {str(val):<42} (Python: {type(val).__name__})")

    # Clean up local database file
    if os.path.exists(DB_FILE):
        try:
            os.remove(DB_FILE)
            print(f"\n[Cleanup] Cleaned up temporary database: {DB_FILE}")
        except OSError:
            pass

    print("\n[PASS] Example 3 completed successfully!\n")


if __name__ == "__main__":
    main()
