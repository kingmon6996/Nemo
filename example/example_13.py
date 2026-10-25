# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 13: Schema Reflection & Column Metadata
Demonstrates inspecting result column names, data types, nullability,
and originating table metadata directly from the Result object.
"""

import os
import nemo

DB_NAME = "example13_columns_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 13: COLUMN METADATA")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating table with mixed column constraints...")
                conn.execute("""
                    CREATE TABLE products (
                        sku VARCHAR NOT NULL,
                        title VARCHAR NOT NULL,
                        price DOUBLE,
                        in_stock BOOLEAN DEFAULT true
                    );
                """)
                conn.execute("INSERT INTO products (sku, title, price, in_stock) VALUES ('SKU-100', 'Laptop Pro', 1299.99, true);")
                print("    -> Schema created and sample record inserted.")

                print("\n[2] Inspecting Result column metadata:")
                result = conn.execute("SELECT sku, title, price, in_stock FROM products;")
                cols = result.columns

                print(f"    Total Columns Returned: {len(cols)}")
                for i, col in enumerate(cols):
                    print(f"    - Column #{i}: name='{col.name:<10}' | table='{col.table}' | nullable={col.nullable} | type_id={col.type}")

        print("\n[PASS] Example 13 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()
