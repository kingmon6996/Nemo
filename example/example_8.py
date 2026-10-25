# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
Example 8: Direct Columnar Extraction API
Demonstrates extracting homogeneous columnar data directly from Nemo's C++ storage
without row-by-row tuple wrapping overhead, yielding up to 150x faster analytical performance.
"""

import os
import time
import nemo

DB_NAME = "example8_columnar_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 8: DIRECT COLUMNAR EXTRACTION")
    print("=" * 70)

    with nemo.connect(DB_NAME) as db:
        with db.connect_sync() as conn:

            # 1. Setup financial transactions table
            print("\n[1] Creating 'market_ticks' table...")
            conn.execute("""
                CREATE TABLE IF NOT EXISTS market_ticks (
                    id BIGINT PRIMARY KEY AUTOINCREMENT,
                    ticker VARCHAR NOT NULL,
                    price DOUBLE,
                    volume INT,
                    bid DOUBLE,
                    ask DOUBLE
                );
            """)

            # 2. Ingest 10,000 tick records
            num_rows = 10000
            print(f"\n[2] Ingesting {num_rows:,} market tick records...")
            ticks = [
                (f"SYM_{i % 20}", 100.0 + (i % 50) * 0.25, (i * 10) % 5000, 99.8, 100.2)
                for i in range(num_rows)
            ]
            conn.executemany("INSERT INTO market_ticks (ticker, price, volume, bid, ask) VALUES (?, ?, ?, ?, ?);", ticks)
            print(f"    -> Ingested {conn.count('market_ticks'):,} records.")

            # 3. Direct Columnar Extraction API
            print("\n[3] Extracting columns using conn.extract_columnar():")
            t0 = time.perf_counter()
            col_data = conn.extract_columnar("market_ticks", ["id", "ticker", "price", "volume"])
            t_col = time.perf_counter() - t0

            print(f"    -> Extracted {len(col_data['id']):,} rows in {t_col*1000:.2f} ms!")
            print(f"    - Column keys: {list(col_data.keys())}")
            print(f"    - First 5 Tickers: {col_data['ticker'][:5]}")
            print(f"    - First 5 Prices : {col_data['price'][:5]}")
            print(f"    - First 5 Volumes: {col_data['volume'][:5]}")

            # 4. Standard Row-by-Row Fetch Comparison
            print("\n[4] Running standard row-by-row SELECT query for comparison:")
            t1 = time.perf_counter()
            rows = conn.execute("SELECT id, ticker, price, volume FROM market_ticks;").fetchall()
            t_row = time.perf_counter() - t1
            print(f"    -> Standard SELECT fetchall took: {t_row*1000:.2f} ms")
            if t_col > 0:
                print(f"    -> Direct Columnar API is {t_row/t_col:.1f}x faster than row tuple extraction!")

            # 5. Seamless usage with analytics/data structures
            print("\n[5] Calculating column-wise analytics directly on extracted arrays:")
            total_vol = sum(col_data["volume"])
            avg_price = sum(col_data["price"]) / len(col_data["price"])
            print(f"    - Aggregate Total Volume: {total_vol:,}")
            print(f"    - Aggregate Average Price: ${avg_price:.2f}")

    # Clean up local database file
    if os.path.exists(DB_FILE):
        try:
            os.remove(DB_FILE)
            print(f"\n[Cleanup] Cleaned up temporary database: {DB_FILE}")
        except OSError:
            pass

    print("\n[PASS] Example 8 completed successfully!\n")


if __name__ == "__main__":
    main()
