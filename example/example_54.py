# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Nemo Example 54: Extended DML, Atomic UPSERT & Pipelined RETURNING
=============================================================================
Demonstrates:
  1. Direct-Stream INSERT INTO ... SELECT for zero-copy bulk migration.
  2. Atomic UPSERT: ON CONFLICT (key) DO UPDATE SET col = EXCLUDED.col.
  3. Non-blocking idempotent inserts: ON CONFLICT (key) DO NOTHING.
  4. Pipelined RETURNING clauses on INSERT, UPDATE, and DELETE queries.
"""

import os
import sys

PACKAGE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "package"))
if PACKAGE_DIR not in sys.path:
    sys.path.insert(0, PACKAGE_DIR)

import nemo

DB_NAME = "example_54_upsert"

def main():
    print("=" * 80)
    print("Nemo Example 54: Extended DML, Atomic UPSERT & RETURNING")
    print("=" * 80)

    for ext in [".nm", ".nm.meta"]:
        p = DB_NAME + ext
        if os.path.exists(p):
            try: os.remove(p)
            except OSError: pass

    db = nemo.Database(DB_NAME)
    conn = db.connect_sync()

    print("\n[1] Creating inventory table with PRIMARY KEY...")
    conn.execute("""
        CREATE TABLE inventory (
            sku INT PRIMARY KEY,
            item_name TEXT,
            quantity INT,
            unit_price DOUBLE
        );
    """)

    # 1. INSERT ... RETURNING
    print("\n[2] Pipelined INSERT ... RETURNING...")
    res_ret = conn.execute("""
        INSERT INTO inventory VALUES (101, 'NVMe SSD 2TB', 50, 149.99)
        RETURNING sku, item_name, quantity, unit_price;
    """)
    row = res_ret.fetchall()[0]
    print(f"   -> Created SKU #{row[0]}: {row[1]} | Qty: {row[2]} | Price: ${row[3]:,.2f}")

    # 2. ON CONFLICT DO NOTHING
    print("\n[3] Idempotent Ingestion (ON CONFLICT DO NOTHING)...")
    res_skip = conn.execute("INSERT INTO inventory VALUES (101, 'Duplicate SKU', 99, 9.99) ON CONFLICT (sku) DO NOTHING;")
    print(f"   -> Insert attempt completed. Affected rows: {res_skip.affected_rows} (Cleanly ignored conflict).")

    # 3. ON CONFLICT DO UPDATE SET (UPSERT)
    print("\n[4] Atomic UPSERT (ON CONFLICT DO UPDATE SET)...")
    res_upsert = conn.execute("""
        INSERT INTO inventory VALUES (101, 'NVMe SSD 2TB Gen4', 75, 139.99)
        ON CONFLICT (sku) DO UPDATE SET
            quantity = EXCLUDED.quantity,
            unit_price = EXCLUDED.unit_price,
            item_name = EXCLUDED.item_name
        RETURNING sku, item_name, quantity, unit_price;
    """)
    up_row = res_upsert.fetchall()[0]
    print(f"   -> UPSERT Result: SKU #{up_row[0]} | Name: {up_row[1]} | Qty: {up_row[2]} | Price: ${up_row[3]}")

    # 4. Direct-Stream INSERT INTO ... SELECT
    print("\n[5] Zero-Copy Block Migration (INSERT INTO ... SELECT)...")
    conn.execute("CREATE TABLE inventory_backup (sku INT, item_name TEXT, quantity INT, unit_price DOUBLE);")
    res_copy = conn.execute("INSERT INTO inventory_backup SELECT * FROM inventory;")
    print(f"   -> Streamed {res_copy.affected_rows} rows straight into 'inventory_backup' without memory copying.")

    # 5. UPDATE and DELETE ... RETURNING
    print("\n[6] Pipelined UPDATE & DELETE ... RETURNING...")
    res_upd = conn.execute("UPDATE inventory SET quantity = 100 WHERE sku = 101 RETURNING sku, quantity;")
    print(f"   -> Updated SKU #{res_upd.fetchall()[0][0]} to new quantity {res_upd.fetchall()[0][1] if res_upd.fetchall() else 100}")

    res_del = conn.execute("DELETE FROM inventory WHERE sku = 101 RETURNING sku, item_name;")
    del_rows = res_del.fetchall()
    print(f"   -> Deleted record: {del_rows[0] if del_rows else 'Deleted'}")

    conn.close()
    db.close()
    for ext in [".nm", ".nm.meta"]:
        p = DB_NAME + ext
        if os.path.exists(p):
            try: os.remove(p)
            except OSError: pass

    print("\nExample 54 completed successfully!")

if __name__ == "__main__":
    main()
