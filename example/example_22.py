# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 22: JSON SQL Functions
Demonstrates calling native JSON functions within SQL queries:
json_extract, json_length, json_contains, and json_set.
"""

import os
import nemo

DB_NAME = "example22_json_funcs_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 22: JSON SQL FUNCTIONS")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Executing SQL queries with JSON functions:")

                # 1. json_extract
                r1 = conn.execute("SELECT json_extract('{\"user\": {\"name\": \"Sophia\", \"role\": \"Lead Architect\"}}', '$.user.name');").fetchone()
                print(f"    - json_extract('$.user.name')       : {r1[0]}")

                # 2. json_length
                r2 = conn.execute("SELECT json_length('[100, 200, 300, 400, 500]');").fetchone()
                print(f"    - json_length('[100, 200, ...]')    : {r2[0]}")

                # 3. json_contains
                r3 = conn.execute("SELECT json_contains('[\"read\", \"write\", \"execute\"]', 'write');").fetchone()
                print(f"    - json_contains('write')            : {r3[0]}")

                # 4. json_set
                r4 = conn.execute("SELECT json_set('{\"status\": \"pending\"}', '$.status', 'completed');").fetchone()
                print(f"    - json_set('$.status', 'completed') : {r4[0]}")

        print("\n[PASS] Example 22 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()
