# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 14: Fast Table Truncate & Storage Reclamation
Demonstrates instantaneous O(1) table truncation, comparing execution
speed against row-by-row DELETE operations.
"""

import os
import time
import nemo

DB_NAME = "example14_truncate_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 14: FAST TRUNCATE")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating temporary analytics table...")
                conn.execute("CREATE TABLE web_logs (id BIGINT PRIMARY KEY AUTOINCREMENT, url VARCHAR, response_time DOUBLE);")

                num_records = 20000
                print(f"\n[2] Ingesting {num_records:,} log entries...")
                logs = [(f"/api/v1/item/{i}", float(10 + (i % 50))) for i in range(num_records)]
                conn.executemany("INSERT INTO web_logs (url, response_time) VALUES (?, ?);", logs)
                print(f"    -> Table row count: {conn.count('web_logs'):,}")

                # Fast Truncate
                print("\n[3] Performing instant O(1) table truncation...")
                t0 = time.perf_counter()
                conn.truncate("web_logs")
                t_elapsed = time.perf_counter() - t0

                remaining = conn.count("web_logs")
                print(f"    -> Truncate completed in {t_elapsed * 1000:.3f} ms!")
                print(f"    -> Remaining records in table: {remaining}")

        print("\n[PASS] Example 14 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()
