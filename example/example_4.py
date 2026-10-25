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
Example 4: Built-in SQL Functions Catalog
Demonstrates practical execution of Nemo's rich built-in functions:
- JSON/JSONB extraction and mutation
- ARRAY manipulation and vector math
- DICT key-value lookups
- String slicing, concatenation, and case conversions
- Mathematical operations
- Range boundary extraction
- Network and Bitwise parsing
"""

import os
import nemo

DB_NAME = "example4_functions_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 4: BUILT-IN SQL FUNCTIONS")
    print("=" * 70)

    with nemo.connect(DB_NAME) as db:
        with db.connect_sync() as conn:

            # 1. JSON and JSONB Document Functions
            print("\n[1] Testing JSON / JSONB Document Functions:")
            res = conn.execute("""
                SELECT 
                    json_extract('{"user": {"id": 101, "name": "Alice"}}', '$.user.name'),
                    json_length('[10, 20, 30, 40]'),
                    json_contains('{"role": "admin", "active": true}', 'admin'),
                    json_set('{"a": 1}', '$.b', 2);
            """).fetchone()
            print(f"    - json_extract('$.user.name') : {res[0]}")
            print(f"    - json_length('[10,20,30,40]'): {res[1]}")
            print(f"    - json_contains('admin')     : {res[2]}")
            print(f"    - json_set('$.b', 2)         : {res[3]}")

            # 2. ARRAY Vector Math and List Functions
            print("\n[2] Testing ARRAY Functions:")
            res = conn.execute("""
                SELECT 
                    array_length('[10, 20, 30, 40, 50]'),
                    array_get('[10, 20, 30, 40, 50]', 3),
                    array_sum('[10, 20, 30, 40]'),
                    array_avg('[10, 20, 30, 40]'),
                    array_append('[1, 2, 3]', 4);
            """).fetchone()
            print(f"    - array_length               : {res[0]}")
            print(f"    - array_get(index 3)         : {res[1]}")
            print(f"    - array_sum                  : {res[2]}")
            print(f"    - array_avg                  : {res[3]}")
            print(f"    - array_append(4)            : {res[4]}")

            # 3. DICT Key-Value Functions
            print("\n[3] Testing DICT Functions:")
            res = conn.execute("""
                SELECT 
                    dict_get('{"host": "localhost", "port": 8080}', 'host'),
                    dict_has_key('{"host": "localhost", "port": 8080}', 'port'),
                    dict_size('{"a": 1, "b": 2, "c": 3}');
            """).fetchone()
            print(f"    - dict_get('host')           : {res[0]}")
            print(f"    - dict_has_key('port')       : {res[1]}")
            print(f"    - dict_size                  : {res[2]}")

            # 4. String & Text Processing Functions
            print("\n[4] Testing String Functions:")
            res = conn.execute("""
                SELECT 
                    upper('nemo database engine'),
                    lower('HIGH CONCURRENCY'),
                    concat('Nemo', ' - ', 'Fastest Embedded DB'),
                    substring('Engine Architecture', 1, 6),
                    trim('   clean string   ');
            """).fetchone()
            print(f"    - upper()                    : {res[0]}")
            print(f"    - lower()                    : {res[1]}")
            print(f"    - concat()                   : {res[2]}")
            print(f"    - substring()                : {res[3]}")
            print(f"    - trim()                     : '{res[4]}'")

            # 5. Numeric & Mathematical Functions
            print("\n[5] Testing Math Functions:")
            res = conn.execute("""
                SELECT 
                    abs(-123.45),
                    round(3.14159, 2),
                    sqrt(144.0),
                    power(2.0, 10.0),
                    mod(17, 5);
            """).fetchone()
            print(f"    - abs(-123.45)               : {res[0]}")
            print(f"    - round(3.14159, 2)          : {res[1]}")
            print(f"    - sqrt(144.0)                : {res[2]}")
            print(f"    - power(2.0, 10.0)           : {res[3]}")
            print(f"    - mod(17, 5)                 : {res[4]}")

            # 6. Range, Network & Utility Functions
            print("\n[6] Testing Range, Network & Utility Functions:")
            res = conn.execute("""
                SELECT 
                    range_lower('[10, 50)'),
                    range_upper('[10, 50)'),
                    range_contains('[10, 50)', 25),
                    host('192.168.1.15/24'),
                    netmask('192.168.1.15/24'),
                    coalesce(null, null, 'default_val');
            """).fetchone()
            print(f"    - range_lower                : {res[0]}")
            print(f"    - range_upper                : {res[1]}")
            print(f"    - range_contains(25)         : {res[2]}")
            print(f"    - host('192.168.1.15/24')    : {res[3]}")
            print(f"    - netmask('/24')             : {res[4]}")
            print(f"    - coalesce()                 : {res[5]}")

    # Clean up local database file
    if os.path.exists(DB_FILE):
        try:
            os.remove(DB_FILE)
            print(f"\n[Cleanup] Cleaned up temporary database: {DB_FILE}")
        except OSError:
            pass

    print("\n[PASS] Example 4 completed successfully!\n")


if __name__ == "__main__":
    main()
