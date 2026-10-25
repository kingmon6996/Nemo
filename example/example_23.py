# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 23: Array & Dict SQL Functions
Demonstrates array_get, array_length, array_sum, array_avg, array_append,
and dict_get / dict_has_key SQL functions.
"""

import os
import nemo

DB_NAME = "example23_array_dict_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 23: ARRAY & DICT SQL FUNCTIONS")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Evaluating SQL ARRAY Functions:")
                arr_str = "[10, 20, 30, 40, 50]"

                r1 = conn.execute(f"SELECT array_length('{arr_str}');").fetchone()
                print(f"    - array_length('{arr_str}') : {r1[0]}")

                r2 = conn.execute(f"SELECT array_get('{arr_str}', 3);").fetchone()
                print(f"    - array_get(index 3)         : {r2[0]}")

                r3 = conn.execute(f"SELECT array_sum('{arr_str}');").fetchone()
                print(f"    - array_sum                  : {r3[0]}")

                r4 = conn.execute(f"SELECT array_avg('{arr_str}');").fetchone()
                print(f"    - array_avg                  : {r4[0]}")

                r5 = conn.execute(f"SELECT array_append('{arr_str}', 60);").fetchone()
                print(f"    - array_append(60)           : {r5[0]}")

                print("\n[2] Evaluating SQL DICT Functions:")
                dict_str = '{"service": "auth", "port": 443, "tls": true}'

                r6 = conn.execute(f"SELECT dict_get('{dict_str}', 'service');").fetchone()
                print(f"    - dict_get('service')        : {r6[0]}")

                r7 = conn.execute(f"SELECT dict_has_key('{dict_str}', 'tls');").fetchone()
                print(f"    - dict_has_key('tls')        : {r7[0]}")

        print("\n[PASS] Example 23 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()
