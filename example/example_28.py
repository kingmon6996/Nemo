# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 28: SIMD Vectorized Aggregations
Demonstrates processing 1024-element CPU chunks using AVX2/NEON instructions
for SUM, AVG, MIN, MAX, and COUNT aggregate queries.
"""

import os
import nemo

DB_NAME = "example28_simd_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 28: SIMD VECTORIZED AGGREGATES")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating sensory data table...")
                conn.execute("CREATE TABLE sensor_readings (id BIGINT PRIMARY KEY AUTOINCREMENT, reading DOUBLE);")

                num_rows = 15000
                print(f"\n[2] Ingesting {num_rows:,} sensor observations...")
                readings = [(float(20.0 + (i % 80)),) for i in range(num_rows)]
                conn.executemany("INSERT INTO sensor_readings (reading) VALUES (?);", readings)
                print(f"    -> Ingested {conn.count('sensor_readings'):,} records.")

                print("\n[3] Calculating SIMD Vectorized Aggregates:")
                row = conn.execute("SELECT sum(reading), avg(reading), min(reading), max(reading) FROM sensor_readings;").fetchone()

                sum_val = float(row[0])
                avg_val = float(row[1])
                min_val = float(row[2])
                max_val = float(row[3])

                print(f"    - Aggregate SUM : {sum_val:,.2f}")
                print(f"    - Aggregate AVG : {avg_val:,.2f}")
                print(f"    - Aggregate MIN : {min_val:,.2f}")
                print(f"    - Aggregate MAX : {max_val:,.2f}")

        print("\n[PASS] Example 28 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()
