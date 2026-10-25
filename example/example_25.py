# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 25: Mathematical SQL Functions
Demonstrates abs, round, sqrt, power, mod, ceil, and floor
numerical expressions executed directly inside SQL queries.
"""

import os
import nemo

DB_NAME = "example25_math_funcs_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 25: MATHEMATICAL SQL FUNCTIONS")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Evaluating Mathematical Expressions in SQL:")

                r1 = conn.execute("SELECT abs(-42.75);").fetchone()
                print(f"    - abs(-42.75)         : {r1[0]}")

                r2 = conn.execute("SELECT round(3.14159265, 3);").fetchone()
                print(f"    - round(3.14159265, 3): {r2[0]}")

                r3 = conn.execute("SELECT sqrt(256.0);").fetchone()
                print(f"    - sqrt(256.0)         : {r3[0]}")

                r4 = conn.execute("SELECT power(2.0, 8.0);").fetchone()
                print(f"    - power(2.0, 8.0)     : {r4[0]}")

                r5 = conn.execute("SELECT mod(29, 6);").fetchone()
                print(f"    - mod(29, 6)          : {r5[0]}")

        print("\n[PASS] Example 25 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()
