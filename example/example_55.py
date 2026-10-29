# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Nemo Example 55: Zero-Copy Schema Evolution, Virtual Views & Fast Indexing
=============================================================================
Demonstrates:
  1. Metadata-only ALTER TABLE ADD COLUMN with DEFAULT and NULL defaults.
  2. Instant O(1) ALTER TABLE RENAME COLUMN and RENAME TO.
  3. Safe idempotent ALTER TABLE DROP COLUMN IF EXISTS.
  4. Logical abstraction with CREATE / REPLACE / DROP VIEW.
  5. High-performance adaptive B+ Tree secondary index management (CREATE / DROP INDEX).
"""

import os
import sys

PACKAGE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "package"))
if PACKAGE_DIR not in sys.path:
    sys.path.insert(0, PACKAGE_DIR)

import nemo

DB_NAME = "example_55_schema_views"

def main():
    print("=" * 80)
    print("Nemo Example 55: Schema Evolution, Virtual Views & Indexing")
    print("=" * 80)

    for ext in [".nm", ".nm.meta"]:
        p = DB_NAME + ext
        if os.path.exists(p):
            try: os.remove(p)
            except OSError: pass

    db = nemo.Database(DB_NAME)
    conn = db.connect_sync()

    print("\n[1] Creating initial table schema...")
    conn.execute("CREATE TABLE products (id INT PRIMARY KEY, name TEXT, price DOUBLE);")
    conn.execute("INSERT INTO products VALUES (1, 'Mechanical Keyboard', 89.99);")
    conn.execute("INSERT INTO products VALUES (2, 'Ergonomic Mouse', 59.99);")

    # 1. Zero-Copy ALTER TABLE ADD COLUMN
    print("\n[2] Zero-Copy Schema Evolution: ADD COLUMN with DEFAULT...")
    conn.execute("ALTER TABLE products ADD COLUMN IF NOT EXISTS status TEXT DEFAULT 'in_stock';")
    res = conn.execute("SELECT id, name, price, status FROM products;")
    for r in res:
        print(f"   * Product #{r[0]}: {r[1]:<22} | Price: ${r[2]:<6.2f} | Status: {r[3]}")

    # 2. RENAME COLUMN
    print("\n[3] O(1) Column Rename...")
    conn.execute("ALTER TABLE products RENAME COLUMN status TO availability;")
    res = conn.execute("SELECT name, availability FROM products WHERE id = 1;")
    print(f"   * Renamed column queried: {res.fetchall()[0]}")

    # 3. Virtual Views: CREATE VIEW
    print("\n[4] Lightweight Virtual Views...")
    conn.execute("CREATE VIEW premium_products AS SELECT id, name, price FROM products WHERE price >= 70.0;")
    res_view = conn.execute("SELECT * FROM premium_products;")
    print("   Querying View 'premium_products':")
    for r in res_view:
        print(f"     -> #{r[0]}: {r[1]} (${r[2]})")

    # 4. Secondary Index Management
    print("\n[5] Fast Secondary Index Creation...")
    conn.execute("CREATE INDEX IF NOT EXISTS idx_prod_price ON products (price);")
    print("   -> B+ Tree Secondary Index 'idx_prod_price' created successfully.")
    conn.execute("DROP INDEX IF EXISTS idx_prod_price;")
    print("   -> B+ Tree Index dropped safely.")

    # 5. Table Rename
    print("\n[6] O(1) Table Identifier Swap (RENAME TO)...")
    conn.execute("ALTER TABLE products RENAME TO catalog_items;")
    res_count = conn.execute("SELECT count(*) FROM catalog_items;")
    print(f"   -> Table renamed to 'catalog_items' (Total rows: {res_count.fetchall()[0][0]}).")

    conn.close()
    db.close()
    for ext in [".nm", ".nm.meta"]:
        p = DB_NAME + ext
        if os.path.exists(p):
            try: os.remove(p)
            except OSError: pass

    print("\nExample 55 completed successfully!")

if __name__ == "__main__":
    main()

