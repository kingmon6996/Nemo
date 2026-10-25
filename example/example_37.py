# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 37: Direct Columnar Extraction for Analytics
Demonstrates extracting database tables directly into {column_name: [values]}
columnar dictionary format without row tuple wrapping overhead.
"""

import os
import nemo

DB_NAME = "example37_columnar_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 37: COLUMNAR EXTRACTION")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating financial trading log table...")
                conn.execute("""
                    CREATE TABLE trades (
                        trade_id BIGINT PRIMARY KEY AUTOINCREMENT,
                        symbol VARCHAR NOT NULL,
                        price DOUBLE NOT NULL,
                        qty INTEGER NOT NULL
                    );
                """)

                print("\n[2] Ingesting 5,000 trades...")
                trades = [(f"SYM_{i % 5}", float(100.0 + (i % 25)), 10 * (i % 50 + 1)) for i in range(5000)]
                conn.executemany("INSERT INTO trades (symbol, price, qty) VALUES (?, ?, ?);", trades)
                print(f"    -> Ingested {conn.count('trades')} trades.")

                print("\n[3] Extracting directly in Columnar format:")
                cols_data = conn.extract_columnar("trades", columns=["symbol", "price", "qty"], limit=1000)

                print(f"    - Column keys extracted: {list(cols_data.keys())}")
                if "symbol" in cols_data:
                    print(f"    - Extracted {len(cols_data['symbol']):,} values per column.")
                    print(f"    - Sample Symbols : {cols_data['symbol'][:5]}")
                    print(f"    - Sample Prices  : {cols_data['price'][:5]}")
                    print(f"    - Sample Quantities: {cols_data['qty'][:5]}")

        print("\n[PASS] Example 37 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()
