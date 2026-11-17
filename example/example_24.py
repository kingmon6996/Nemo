# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 24: String SQL Functions
Demonstrates upper, lower, concat, substring, trim, and length
transformations executed natively by the query engine.
"""

import nemo

DB_NAME = "example24_string_funcs_demo"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 24: STRING SQL FUNCTIONS")
    print("=" * 70)

    import shutil; shutil.rmtree(f".nemo/{DB_NAME}", ignore_errors=True)
    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Executing SQL queries with String functions:")

                r1 = conn.execute("SELECT upper('nemo database engine');").fetchone()
                print(f"    - upper('nemo database engine') : {r1[0]}")

                r2 = conn.execute("SELECT lower('ULTRA HIGH THROUGHPUT');").fetchone()
                print(f"    - lower('ULTRA...')             : {r2[0]}")

                r3 = conn.execute("SELECT concat('Cloud', ' - ', 'Native');").fetchone()
                print(f"    - concat('Cloud', '-', 'Native') : {r3[0]}")

                r4 = conn.execute("SELECT substring('Embedded Database', 10, 8);").fetchone()
                print(f"    - substring('Embedded Database') : {r4[0]}")

                r5 = conn.execute("SELECT trim('   padded text   ');").fetchone()
                print(f"    - trim('   padded text   ')      : '{r5[0]}'")

        print("\n[PASS] Example 24 completed successfully!\n")
    finally:
        import shutil; shutil.rmtree(f".nemo/{DB_NAME}", ignore_errors=True)
if __name__ == "__main__":
    main()
