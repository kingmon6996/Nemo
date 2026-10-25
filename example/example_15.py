# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 15: Numeric Datatypes Deep-Dive
Demonstrates storage and precision boundaries of TINYINT, SMALLINT,
INTEGER, BIGINT, REAL (float32), and DOUBLE (float64).
"""

import os
import nemo

DB_NAME = "example15_numeric_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 15: NUMERIC DATATYPES")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating numeric benchmark schema...")
                conn.execute("""
                    CREATE TABLE numeric_data (
                        id BIGINT PRIMARY KEY AUTOINCREMENT,
                        c_tinyint TINYINT,
                        c_smallint SMALLINT,
                        c_integer INTEGER,
                        c_bigint BIGINT,
                        c_real REAL,
                        c_double DOUBLE
                    );
                """)

                print("\n[2] Inserting boundary and extreme numeric values...")
                conn.execute("""
                    INSERT INTO numeric_data (c_tinyint, c_smallint, c_integer, c_bigint, c_real, c_double)
                    VALUES (127, 32767, 2147483647, 9223372036854775807, 3.141592, 2.718281828459045);
                """)

                print("\n[3] Reading and verifying numeric integrity:")
                row = conn.execute("SELECT c_tinyint, c_smallint, c_integer, c_bigint, c_real, c_double FROM numeric_data;").fetchone()
                print(f"    - TINYINT  (8-bit signed)   : {row[0]}")
                print(f"    - SMALLINT (16-bit signed)  : {row[1]}")
                print(f"    - INTEGER  (32-bit signed)  : {row[2]}")
                print(f"    - BIGINT   (64-bit signed)  : {row[3]}")
                print(f"    - REAL     (32-bit float)   : {row[4]}")
                print(f"    - DOUBLE   (64-bit float)   : {row[5]}")

        print("\n[PASS] Example 15 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()
