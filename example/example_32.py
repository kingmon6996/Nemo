# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 32: Filter Expressions & Range Queries
Demonstrates BETWEEN, equality, greater/less than, and compound conditions
executed against indexed and vectorized scans.
"""

import os
import nemo

DB_NAME = "example32_filters_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 32: FILTER EXPRESSIONS")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating inventory table...")
                conn.execute("""
                    CREATE TABLE inventory (
                        item_id BIGINT PRIMARY KEY AUTOINCREMENT,
                        sku VARCHAR NOT NULL,
                        stock_level INTEGER NOT NULL,
                        unit_cost DOUBLE NOT NULL
                    );
                """)

                items = [
                    (f"ITEM-{i}", 10 + (i * 5), float(5.0 + (i * 2.5)))
                    for i in range(1, 20)
                ]
                conn.executemany("INSERT INTO inventory (sku, stock_level, unit_cost) VALUES (?, ?, ?);", items)
                print(f"    -> Ingested {len(items)} inventory records.")

                print("\n[2] Executing BETWEEN Range Query (stock_level BETWEEN 30 AND 70):")
                range_query = "SELECT item_id, sku, stock_level, unit_cost FROM inventory WHERE stock_level BETWEEN 30 AND 70;"
                res = conn.execute(range_query)
                for r in res.fetchall():
                    print(f"    - Item #{r[0]}: {r[1]:<10} | Stock: {r[2]:>3} | Cost: ${float(r[3]):.2f}")

        print("\n[PASS] Example 32 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()
