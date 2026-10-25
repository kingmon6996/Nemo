# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 30: Cost-Based Optimizer (CBO) & EXPLAIN
Demonstrates inspecting physical query execution plans, estimated costs,
vectorized operators, and join algorithms with EXPLAIN.
"""

import os
import nemo

DB_NAME = "example30_cbo_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 30: COST-BASED OPTIMIZER")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating table and inserting test records...")
                conn.execute("CREATE TABLE orders (order_id BIGINT PRIMARY KEY AUTOINCREMENT, total DOUBLE);")
                data = [(float(10.0 + (i % 200)),) for i in range(500)]
                conn.executemany("INSERT INTO orders (total) VALUES (?);", data)

                print("\n[2] Explaining Vectorized Aggregation Plan:")
                plan = conn.explain("SELECT sum(total), avg(total) FROM orders;")
                print("    Physical Plan Analysis:")
                for line in plan.splitlines():
                    print(f"      {line}")

                print("\n[3] Explaining Point Lookup by Primary Key:")
                plan_point = conn.explain("SELECT * FROM orders WHERE order_id = 42;")
                print("    Physical Plan Analysis:")
                for line in plan_point.splitlines():
                    print(f"      {line}")

        print("\n[PASS] Example 30 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()
