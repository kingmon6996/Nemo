# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 31: Common Table Expressions (CTEs)
Demonstrates defining reusable modular query logic with WITH clauses,
enabling cleaner relational transformations.
"""

import os
import nemo

DB_NAME = "example31_cte_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 31: COMMON TABLE EXPRESSIONS")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating regional sales table...")
                conn.execute("""
                    CREATE TABLE sales (
                        id BIGINT PRIMARY KEY AUTOINCREMENT,
                        region VARCHAR NOT NULL,
                        amount DOUBLE NOT NULL
                    );
                """)

                records = [
                    ("US-East", 45000.0),
                    ("US-East", 55000.0),
                    ("US-West", 30000.0),
                    ("EU-Central", 60000.0),
                    ("EU-Central", 40000.0),
                ]
                conn.executemany("INSERT INTO sales (region, amount) VALUES (?, ?);", records)
                print(f"    -> Ingested {len(records)} sales records.")

                print("\n[2] Executing CTE Query (WITH high_value_regions AS ...):")
                cte_query = """
                    WITH high_value_regions AS (
                        SELECT id, region, amount FROM sales WHERE amount >= 40000.0
                    )
                    SELECT id, region, amount FROM high_value_regions;
                """
                res = conn.execute(cte_query)
                for r in res.fetchall():
                    print(f"    - Sale #{r[0]}: Region: {r[1]:<12} | Amount: ${float(r[2]):,.2f}")

        print("\n[PASS] Example 31 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()
